#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include "stablepackage_p.h"
#include "wintrustfixture.h"
#include "fixturehelper.h"
#include <winioctl.h>
#include <type_traits>

using namespace zzlogg::update;
using detail::StablePackage;
namespace {
constexpr auto abcHash="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h):value(h) {}
    ~Handle() { if(value!=INVALID_HANDLE_VALUE && value) CloseHandle(value); }
};
struct Files {
    QTemporaryDir root;
    std::wstring parent, file, target;
    Files() {
        QDir(root.path()).mkpath("ancestor/parent");
        QDir(root.path()).mkpath("target");
        parent=QDir::toNativeSeparators(root.path()+"/ancestor/parent").toStdWString();
        target=QDir::toNativeSeparators(root.path()+"/target").toStdWString();
        file=parent+L"\\package.exe";
        QFile output(QString::fromStdWString(file));
        if(output.open(QIODevice::WriteOnly)) output.write("abc");
    }
};
bool setJunction(HANDLE directory,const std::wstring& target) {
    struct MountPoint {
        DWORD tag; WORD length,reserved;
        WORD substituteOffset,substituteLength,printOffset,printLength;
        wchar_t buffer[2048];
    } data{};
    const auto substitute=L"\\??\\"+target;
    if(substitute.size()+target.size()+2>2048) return false;
    data.tag=IO_REPARSE_TAG_MOUNT_POINT;
    data.substituteLength=static_cast<WORD>(substitute.size()*sizeof(wchar_t));
    data.printOffset=data.substituteLength+sizeof(wchar_t);
    data.printLength=static_cast<WORD>(target.size()*sizeof(wchar_t));
    data.length=8+data.printOffset+data.printLength+sizeof(wchar_t);
    memcpy(data.buffer,substitute.c_str(),data.substituteLength);
    memcpy(reinterpret_cast<BYTE*>(data.buffer)+data.printOffset,target.c_str(),data.printLength);
    DWORD returned=0;
    return DeviceIoControl(directory,FSCTL_SET_REPARSE_POINT,&data,data.length+8,
                           nullptr,0,&returned,nullptr)!=FALSE;
}
HANDLE openDirectory(const std::wstring& path,DWORD access) {
    return CreateFileW(path.c_str(),access,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
}
InstalledRelease installed() {
    InstalledRelease value;
    value.version=*parseVersion("26.09.00"); value.releaseSequence=1;
    value.channel="stable"; value.os="windows"; value.arch="x64";
    value.distribution=Distribution::Installer; value.osVersion={10,0,22631};
    value.dataSchema=1; value.updaterProtocol=1;
    return value;
}
struct SelectionFixture {
    VerificationContext context=update_fixture::context();
    UpdateSelection selection;
    explicit SelectionFixture(bool production=false) {
        auto payload=update_fixture::payload();
        auto& artifact=payload["artifacts"][0];
        artifact["distribution"]="installer"; artifact["format"]="nsis-exe";
        artifact["size"]="3"; artifact["sha256"]=abcHash;
        auto envelope=update_fixture::envelope(payload.dump());
        if(production) {
            // Isolated test key, never linked into the production execution library.
            std::array<std::uint8_t,32> seed{},publicKey{}; seed.fill(0x42);
            std::array<std::uint8_t,64> secret{},signature{};
            crypto_ed25519_key_pair(secret.data(),publicKey.data(),seed.data());
            context.environment=TrustEnvironment::Production;
            context.keys={{"isolated",{publicKey.begin(),publicKey.end()},KeyPurpose::Production}};
            const auto raw=payload.dump();
            const auto message="ZzLogg update manifest v1\nisolated\n"+raw;
            crypto_ed25519_sign(signature.data(),secret.data(),reinterpret_cast<const std::uint8_t*>(message.data()),message.size());
            envelope=nlohmann::json{{"schema",1},{"keyId","isolated"},
                {"payload",detail::encodeBase64(raw)},
                {"signature",detail::encodeBase64({reinterpret_cast<const char*>(signature.data()),signature.size()})}}.dump();
        }
        const auto verified=verifyManifest(envelope,context);
        if(verified.value) {
            context.lastAccepted=verified.value->acceptedMetadata();
            const auto value=makeUpdateSelection(*verified.value,installed());
            if(value) selection=*value;
        }
    }
};
}
class PackageVerificationTest:public QObject {
    Q_OBJECT
private slots:
    // Accepting nonzero LONG, timestamp signers, missing policy or skipping close
    // must break these assertions on the production algorithm's return values.
    void authenticodeAlgorithm() {
        Files files; StablePackage lease;
        QCOMPARE(lease.open(files.file),PackageVerificationError::None);
        wintrust_fixture::State fixture;
        fixture.expectedFile=lease.handle(); fixture.expectedPath=lease.path();
        wintrust_fixture::active=&fixture;
        const detail::PublisherPolicy allowed={wintrust_fixture::abcFingerprint()};
        auto call=[&](const detail::PublisherPolicy& policy) {
            return detail::checkAuthenticode(lease.handle(),lease.path(),policy,wintrust_fixture::api());
        };
        QCOMPARE(call(allowed),PackageVerificationError::None);
        QCOMPARE(fixture.verifies,1); QCOMPARE(fixture.closes,1); QVERIFY(fixture.correctParameters);
        QCOMPARE(call({}),PackageVerificationError::PublisherPolicyMissing);
        QCOMPARE(fixture.verifies,1);
        for(const LONG failure:{LONG(1),LONG(TRUST_E_NOSIGNATURE),LONG(CERT_E_REVOKED),LONG(CRYPT_E_REVOCATION_OFFLINE)}) {
            fixture.status=failure;
            QCOMPARE(call(allowed),PackageVerificationError::SignatureUntrusted);
            QCOMPARE(fixture.verifies,fixture.closes);
        }
        fixture.status=0;
        auto wrong=allowed; wrong[0][0]^=1;
        QCOMPARE(call(wrong),PackageVerificationError::PublisherMismatch);
        fixture.hasProvider=false;
        QCOMPARE(call(allowed),PackageVerificationError::SignatureUntrusted); fixture.hasProvider=true;
        fixture.hasSigner=false;
        QCOMPARE(call(allowed),PackageVerificationError::SignatureUntrusted); fixture.hasSigner=true;
        fixture.signer.csCertChain=0;
        QCOMPARE(call(allowed),PackageVerificationError::SignatureUntrusted); fixture.signer.csCertChain=1;
        fixture.chain.pCert=nullptr;
        QCOMPARE(call(allowed),PackageVerificationError::SignatureUntrusted); fixture.chain.pCert=&fixture.certificate;
        QCOMPARE(fixture.verifies,fixture.closes); QVERIFY(fixture.correctParameters);
        auto incomplete=wintrust_fixture::api(); incomplete.signer=nullptr;
        QCOMPARE(detail::checkAuthenticode(lease.handle(),lease.path(),allowed,incomplete),PackageVerificationError::SignatureUntrusted);
    }
    void realUnsignedFileIsRejected() {
        Files files; StablePackage lease;
        QCOMPARE(lease.open(files.file),PackageVerificationError::None);
        QCOMPARE(detail::verifyAuthenticode(lease.handle(),lease.path(),{wintrust_fixture::abcFingerprint()}),
                 PackageVerificationError::SignatureUntrusted);
    }
    void productionCompositionRemainsClosed() {
        static_assert(!std::is_default_constructible_v<VerifiedPackage>);
        static_assert(!std::is_copy_constructible_v<VerifiedPackage>);
        static_assert(std::is_nothrow_move_constructible_v<VerifiedPackage>);
        Files files;
        SelectionFixture test;
        QVERIFY(!test.selection.signedEnvelope.empty());
        auto result=verifyPackageForExecution(test.selection,test.context,installed(),files.file);
        QCOMPARE(result.error,PackageVerificationError::SelectionRejected); QVERIFY(!result.package);
        SelectionFixture production(true);
        QVERIFY(!production.selection.signedEnvelope.empty());
        result=verifyPackageForExecution(production.selection,production.context,installed(),files.file);
        QCOMPARE(result.error,PackageVerificationError::PublisherPolicyMissing); QVERIFY(!result.package);
        // The missing policy is checked before even trying to open this path.
        result=verifyPackageForExecution(production.selection,production.context,installed(),L"not a path");
        QCOMPARE(result.error,PackageVerificationError::PublisherPolicyMissing); QVERIFY(!result.package);
        auto altered=production.selection; altered.artifact.sha256[0]='0';
        result=verifyPackageForExecution(altered,production.context,installed(),files.file);
        QCOMPARE(result.error,PackageVerificationError::SelectionRejected); QVERIFY(!result.package);
        altered=production.selection; altered.signedEnvelope[0]='x';
        result=verifyPackageForExecution(altered,production.context,installed(),files.file);
        QCOMPARE(result.error,PackageVerificationError::SelectionRejected); QVERIFY(!result.package);
        auto stale=production.context; stale.now=1800003600;
        result=verifyPackageForExecution(production.selection,stale,installed(),files.file);
        QCOMPARE(result.error,PackageVerificationError::SelectionRejected); QVERIFY(!result.package);
        stale=production.context; ++stale.lastAccepted->sequence;
        result=verifyPackageForExecution(production.selection,stale,installed(),files.file);
        QCOMPARE(result.error,PackageVerificationError::SelectionRejected); QVERIFY(!result.package);
        auto changed=installed(); changed.arch="arm64";
        result=verifyPackageForExecution(production.selection,production.context,changed,files.file);
        QCOMPARE(result.error,PackageVerificationError::SelectionRejected); QVERIFY(!result.package);
    }
    // Missing stable handle/hash checks must fail these real-file assertions.
    void contentAndIdentity() {
        Files files; StablePackage lease;
        QCOMPARE(lease.open(files.file),PackageVerificationError::None);
        QVERIFY(lease.identityUnchanged());
        QCOMPARE(lease.verifyContent(3,abcHash),PackageVerificationError::None);
        QCOMPARE(lease.verifyContent(4,abcHash),PackageVerificationError::SizeMismatch);
        QCOMPARE(lease.verifyContent(0,abcHash),PackageVerificationError::SizeMismatch);
        QCOMPARE(lease.verifyContent(512ULL*1024*1024+1,abcHash),PackageVerificationError::SizeMismatch);
        QCOMPARE(lease.verifyContent(3,std::string(64,'0')),PackageVerificationError::HashMismatch);
        QCOMPARE(lease.verifyContent(3,std::string(64,'A')),PackageVerificationError::HashMismatch);
        QCOMPARE(lease.verifyContent(3,"abc"),PackageVerificationError::HashMismatch);
    }
    void streamingHashAndReadFailure() {
        Files files;
        {
            QFile output(QString::fromStdWString(files.file));
            QVERIFY(output.open(QIODevice::WriteOnly));
            QCOMPARE(output.write(QByteArray(1000000,'a')),qint64(1000000));
        }
        StablePackage lease;
        QCOMPARE(lease.open(files.file),PackageVerificationError::None);
        // Published SHA-256 million-'a' vector; spans more than fifteen reads.
        QCOMPARE(lease.verifyContent(1000000,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"),
                 PackageVerificationError::None);
        Handle blocker(CreateFileW(files.file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));
        QVERIFY(blocker.value!=INVALID_HANDLE_VALUE);
        OVERLAPPED offset{};
        QVERIFY(LockFileEx(blocker.value,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&offset));
        const auto failed=lease.verifyContent(1000000,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
        QVERIFY(UnlockFileEx(blocker.value,0,1,0,&offset));
        QCOMPARE(failed,PackageVerificationError::FileUnavailable);
    }
    void rejectsAmbiguousPaths() {
        const std::vector<std::wstring> paths={L"",L"a.exe",L"C:foo",L"\\foo",L"\\\\server\\x",
            L"\\\\?\\C:\\a.exe",L"\\\\.\\C:\\a.exe",L"C:/a.exe",L"C:\\a.exe:ads",L"C:\\a\\..\\b",
            L"C:\\a\\.\\b",L"C:\\a\\\\b",L"C:\\a.\\b",L"C:\\a \\b",L"C:\\NUL.exe",
            L"C:\\COM1.txt",L"C:\\LPT9",L"C:\\CONIN$",L"C:\\a*",std::wstring(L"C:\\a\0b",6)};
        for(const auto& path:paths) { StablePackage lease; QCOMPARE(lease.open(path),PackageVerificationError::InvalidPath); }
    }
    void refusesExistingWriterAndDeleteHandle() {
        Files files;
        for(const DWORD access:{static_cast<DWORD>(GENERIC_WRITE),static_cast<DWORD>(DELETE)}) {
            Handle writer(CreateFileW(files.file.c_str(),access,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                nullptr,OPEN_EXISTING,0,nullptr));
            QVERIFY(writer.value!=INVALID_HANDLE_VALUE);
            StablePackage lease; QCOMPARE(lease.open(files.file),PackageVerificationError::FileUnavailable);
        }
    }
    void refusesWritableMappingAfterFileHandleClosed() {
        Files files;
        HANDLE writer=CreateFileW(files.file.c_str(),GENERIC_READ|GENERIC_WRITE,
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
        QVERIFY(writer!=INVALID_HANDLE_VALUE);
        Handle mapping(CreateFileMappingW(writer,nullptr,PAGE_READWRITE,0,0,nullptr));
        CloseHandle(writer);
        QVERIFY(mapping.value!=nullptr);
        auto view=MapViewOfFile(mapping.value,FILE_MAP_WRITE,0,0,0);
        QVERIFY(view!=nullptr);
        StablePackage lease;
        const auto result=lease.open(files.file);
        UnmapViewOfFile(view);
        QCOMPARE(result,PackageVerificationError::FileUnavailable);
    }
    // Removing FILE_SHARE_READ-only protection must expose these operations.
    void leaseProtectsFileAndAncestorsAcrossMove() {
        Files files;
        {
            StablePackage original; QCOMPARE(original.open(files.file),PackageVerificationError::None);
            StablePackage moved(std::move(original));
            QVERIFY(!original.identityUnchanged());
            QVERIFY(moved.identityUnchanged());
            Handle writer(CreateFileW(files.file.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                nullptr,OPEN_EXISTING,0,nullptr));
            QVERIFY(writer.value==INVALID_HANDLE_VALUE);
            QVERIFY(!DeleteFileW(files.file.c_str()));
            QVERIFY(!MoveFileW(files.file.c_str(),(files.file+L".moved").c_str()));
            QVERIFY(!MoveFileW(files.parent.c_str(),(files.parent+L".moved").c_str()));
            const auto ancestor=QDir::toNativeSeparators(files.root.path()+"/ancestor").toStdWString();
            for(const auto& directory:{files.parent,ancestor}) {
                QVERIFY(!MoveFileW(directory.c_str(),(directory+L".moved").c_str()));
                Handle directoryWriter(openDirectory(directory,GENERIC_WRITE));
                QVERIFY(directoryWriter.value!=INVALID_HANDLE_VALUE);
                QVERIFY(!setJunction(directoryWriter.value,files.target));
                QCOMPARE(GetLastError(),DWORD(ERROR_DIR_NOT_EMPTY));
                // Metadata access does not participate in ordinary write sharing.
                // Actually issue FSCTL_SET_REPARSE_POINT, on both retained levels.
                Handle attributes(openDirectory(directory,FILE_WRITE_ATTRIBUTES));
                QVERIFY(attributes.value!=INVALID_HANDLE_VALUE);
                QVERIFY(!setJunction(attributes.value,files.target));
                QCOMPARE(GetLastError(),DWORD(ERROR_DIR_NOT_EMPTY));
            }
            StablePackage assigned; assigned=std::move(moved);
            QVERIFY(!moved.identityUnchanged());
            QVERIFY(assigned.identityUnchanged());
        }
        Handle writer(CreateFileW(files.file.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));
        QVERIFY(writer.value!=INVALID_HANDLE_VALUE);
    }
    void releasedLeaseAllowsRenameAndDelete() {
        Files files;
        { StablePackage lease; QCOMPARE(lease.open(files.file),PackageVerificationError::None); }
        QVERIFY(MoveFileW(files.file.c_str(),(files.file+L".moved").c_str()));
        QVERIFY(DeleteFileW((files.file+L".moved").c_str()));
        QVERIFY(MoveFileW(files.parent.c_str(),(files.parent+L".moved").c_str()));
    }
    void failedOpenAndMoveAssignmentReleaseOwnedHandles() {
        Files first,second;
        StablePackage lease;
        QCOMPARE(lease.open(first.parent+L"\\missing.exe"),PackageVerificationError::FileUnavailable);
        QVERIFY(MoveFileW(first.parent.c_str(),(first.parent+L".moved").c_str()));
        QVERIFY(MoveFileW((first.parent+L".moved").c_str(),first.parent.c_str()));
        QCOMPARE(lease.open(first.file),PackageVerificationError::None);
        StablePackage replacement;
        QCOMPARE(replacement.open(second.file),PackageVerificationError::None);
        lease=std::move(replacement);
        QVERIFY(DeleteFileW(first.file.c_str()));
        QVERIFY(!DeleteFileW(second.file.c_str()));
        QVERIFY(!replacement.identityUnchanged());
        QVERIFY(lease.identityUnchanged());
    }
    void rejectsHardLinksAndDirectories() {
        Files files;
        const auto link=files.target+L"\\hard.exe";
        QVERIFY(CreateHardLinkW(link.c_str(),files.file.c_str(),nullptr));
        StablePackage lease;
        QCOMPARE(lease.open(files.file),PackageVerificationError::FileUnavailable);
        QCOMPARE(lease.open(files.parent),PackageVerificationError::FileUnavailable);
    }
    void rejectsJunctionAncestor() {
        Files files;
        const auto link=QDir::toNativeSeparators(files.root.path()+"/junction").toStdWString();
        QVERIFY(CreateDirectoryW(link.c_str(),nullptr));
        { Handle dir(openDirectory(link,GENERIC_WRITE)); QVERIFY(dir.value!=INVALID_HANDLE_VALUE); QVERIFY(setJunction(dir.value,files.parent)); }
        StablePackage lease;
        const auto result=lease.open(link+L"\\package.exe");
        QVERIFY(RemoveDirectoryW(link.c_str()));
        QCOMPARE(result,PackageVerificationError::FileUnavailable);
    }
    void rejectsFileSymlink() {
        Files files; const auto link=files.target+L"\\symbolic.exe";
        if(!CreateSymbolicLinkW(link.c_str(),files.file.c_str(),SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
            const auto error=GetLastError();
            if(error==ERROR_PRIVILEGE_NOT_HELD || error==ERROR_ACCESS_DENIED)
                QSKIP("File symbolic link creation privilege unavailable");
            QFAIL(qPrintable(QString("CreateSymbolicLink error %1").arg(error)));
        }
        StablePackage lease; const auto result=lease.open(link);
        QVERIFY(DeleteFileW(link.c_str()));
        QCOMPARE(result,PackageVerificationError::FileUnavailable);
    }
};
QTEST_GUILESS_MAIN(PackageVerificationTest)
#include "packageverificationtest.moc"
