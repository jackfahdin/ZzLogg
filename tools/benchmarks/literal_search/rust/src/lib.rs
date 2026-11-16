use aho_corasick::{AhoCorasick, MatchKind};
use memchr::memmem::Finder;
use std::slice;

#[repr(C)]
pub struct ByteView {
    data: *const u8,
    size: usize,
    eligible: u8,
}
// These functions are an internal probe ABI: the C++ adapter owns valid buffers and
// the handle, checks dimensions, and calls scan synchronously before freeing them.
unsafe fn bytes<'a>(view: &ByteView) -> &'a [u8] {
    if view.size == 0 { &[] } else { slice::from_raw_parts(view.data, view.size) }
}
enum Engine {
    Memchr(Vec<Finder<'static>>),
    Aho(AhoCorasick),
}
struct Matcher {
    engine: Engine,
    count: usize,
}
#[no_mangle]
pub unsafe extern "C" fn literal_new(patterns: *const ByteView, count: usize, aho: bool) -> *mut std::ffi::c_void {
    if count == 0 { return std::ptr::null_mut(); }
    let patterns = slice::from_raw_parts(patterns, count);
    let needles: Vec<&[u8]> = patterns.iter().map(|p| bytes(p)).collect();
    let engine = if aho {
        match AhoCorasick::builder().match_kind(MatchKind::Standard).build(&needles) {
            Ok(ac) => Engine::Aho(ac),
            Err(_) => return std::ptr::null_mut(),
        }
    } else {
        Engine::Memchr(needles.iter().map(|n| Finder::new(n).into_owned()).collect())
    };
    Box::into_raw(Box::new(Matcher { engine, count })).cast()
}
#[no_mangle]
pub unsafe extern "C" fn literal_free(handle: *mut std::ffi::c_void) {
    drop(Box::from_raw(handle.cast::<Matcher>()));
}
#[no_mangle]
pub unsafe extern "C" fn literal_memory(handle: *const std::ffi::c_void) -> usize {
    match &(*handle.cast::<Matcher>()).engine {
        Engine::Aho(ac) => ac.memory_usage(),
        _ => 0, // Not an allocator accounting API; zero means unreported.
    }
}
#[no_mangle]
pub unsafe extern "C" fn literal_scan(handle: *const std::ffi::c_void, lines: *const ByteView,
                                      length: usize, output: *mut u8) {
    let matcher = &*handle.cast::<Matcher>();
    if length == 0 { return; }
    let lines = slice::from_raw_parts(lines, length);
    let output = slice::from_raw_parts_mut(output, length * matcher.count);
    for (row, line) in lines.iter().enumerate() {
        if line.eligible == 0 { continue; } // C++ already filled the Qt fallback result.
        let result = &mut output[row * matcher.count..(row + 1) * matcher.count];
        match &matcher.engine {
            Engine::Memchr(finders) => {
                for (cell, finder) in result.iter_mut().zip(finders) {
                    *cell = u8::from(finder.find(bytes(line)).is_some());
                }
            }
            Engine::Aho(ac) => {
                let mut remaining = matcher.count;
                for found in ac.find_overlapping_iter(bytes(line)) {
                    let cell = &mut result[found.pattern().as_usize()];
                    if *cell == 0 { *cell = 1; remaining -= 1; }
                    if remaining == 0 { break; }
                }
            }
        }
    }
}
