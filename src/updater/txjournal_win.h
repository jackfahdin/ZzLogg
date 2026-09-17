#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace zzlogg::updater {

enum class TxJournalError { None, InvalidRoot, Unavailable, Exists, Corrupt };
// Value names avoid the windows.h CreateFile/ReplaceFile/DeleteFile macros.
enum class TxOperation : std::uint32_t {
    Backup = 1,
    Replace = 2,
    Create = 3,
    Delete = 4,
    Registry = 5,
    Complete = 6,
};

// One durable journal line. Paths are normalized absolute local paths (the
// shared installation-path rules, not a new validation set); oldRegistryValue
// is only meaningful for Registry. sha256/size describe the file content
// the operation expects; their semantics belong to the transaction engine.
struct TxJournalRecord {
    std::uint64_t seq = 0;
    TxOperation op = TxOperation::Backup;
    std::uint32_t flags = 0;
    std::wstring target;
    std::wstring backup;
    std::array<std::uint8_t, 32> sha256{};
    std::uint64_t size = 0;
    std::wstring oldRegistryValue;
};
bool operator==(const TxJournalRecord& a, const TxJournalRecord& b);
bool operator!=(const TxJournalRecord& a, const TxJournalRecord& b);

struct TxJournalOptions {
    // SDDL principal strings granted modify rights on the protected
    // transaction directory, and principals granted read/execute only.
    // Injectable so tests act as the principal without touching real
    // ProgramData; production is Administrators/SYSTEM write + authenticated
    // users read.
    std::vector<std::wstring> writers;
    std::vector<std::wstring> readers;
    // Durability flush applied after the header write and each record write;
    // the default issues FlushFileBuffers. Injectable (as a HANDLE, void* to
    // keep this header Qt/Windows-free) so tests observe that every append is
    // really followed by a flush; production leaves it empty.
    std::function<bool(void*)> flush;
    static TxJournalOptions production();
};

enum class TxVolumeCheck { Ok, InvalidRoot, Unavailable, Insufficient };
// Fail-closed space preflight: rejects nonstandard/network roots, walks to the
// nearest existing non-reparse ancestor for the free-space query, and compares
// against staging + backup + a fixed journal reserve with saturating arithmetic.
TxVolumeCheck checkVolumeSpace(const std::wstring& root, std::uint64_t stagingBytes,
    std::uint64_t backupBytes, std::uint64_t* availableBytes = nullptr);

// Append-only durable transaction journal living in an exclusively created
// per-transaction directory. Every record is written and flushed before the
// operation it describes may run; recovery is a strict parse that refuses any
// torn, unknown, out-of-order, or foreign-transaction content.
class TxJournal {
public:
    TxJournal();
    ~TxJournal();
    TxJournal(TxJournal&&) noexcept;
    TxJournal& operator=(TxJournal&&) noexcept;
    TxJournal(const TxJournal&) = delete;
    TxJournal& operator=(const TxJournal&) = delete;
    // Exclusively creates root\<txid as 16 lowercase hex>\ with the injected
    // ACL (atomic NtCreateFile creation, ancestors pinned against rename and
    // reparse), then creates journal.log and durably writes the header.
    // Rejects reparse/network roots; an existing directory fails as Exists.
    TxJournalError open(const std::wstring& root, std::uint64_t txid, const TxJournalOptions& options);
    // Writes the record then flushes. seq must be the next sequence number;
    // appending after Complete is refused. Any write/flush failure latches the
    // journal into a failed state and all later appends fail.
    TxJournalError append(const TxJournalRecord& record);
    const std::wstring& directory() const;
    // Strictly parses an existing journal for the expected transaction. Any
    // corruption (torn tail, unknown op, sequence gap, transaction mismatch)
    // refuses recovery entirely: Corrupt with an empty executed set. Replay is
    // read-only and idempotent.
    static TxJournalError replay(const std::wstring& directory, std::uint64_t txid,
        std::vector<TxJournalRecord>& executed);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
