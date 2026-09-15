#include <QtTest>
#include <QFile>
#include <QLockFile>
#include <QTemporaryDir>
#include <json.hpp>
#include "zzlogg/updateqt/updatestate.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif
using namespace zzlogg::updateqt;
using zzlogg::update::AcceptedMetadata;
namespace {
QByteArray bytes(const QString& path) { QFile f(path); if(!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
bool write(const QString& path,const QByteArray& data) { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(data)==data.size(); }
}
class StateTest:public QObject {
    Q_OBJECT
private slots:
    void initialAndRoundTrip() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto path=dir.filePath("updates/state.json");
        UpdateStateStore store(path);
        QVERIFY(store.read(Channel::Stable).value);
        QVERIFY(!QFile::exists(path));
        AcceptedMetadata record{2,{}}; record.payloadDigest[0]=0x42;
        QCOMPARE(store.accept(Channel::Stable,record,1800000000),StateError::None);
        const auto state=UpdateStateStore(path).read(Channel::Stable);
        QVERIFY(state.value && state.value->accepted);
        QCOMPARE(state.value->accepted->sequence,std::uint64_t(2));
        QVERIFY(state.value->accepted->payloadDigest==record.payloadDigest);
        QCOMPARE(state.value->lastSuccess,qint64(1800000000));
        QCOMPARE(state.value->nextAttempt,qint64(1800086400));
        QVERIFY(!store.read(Channel::Preview).value->accepted);
    }
    void interleavingDoesNotLoseState() {
        QTemporaryDir dir; const auto path=dir.filePath("state.json");
        UpdateStateStore a(path),b(path);
        QVERIFY(b.read(Channel::Stable).value);
        AcceptedMetadata newer{3,{}}; newer.payloadDigest[5]=0x99;
        QCOMPARE(a.accept(Channel::Stable,newer,100),StateError::None);
        QCOMPARE(b.accept(Channel::Preview,{9,{}},101),StateError::None);
        QCOMPARE(b.accept(Channel::Stable,{2,{}},102),StateError::Replay);
        QCOMPARE(b.accept(Channel::Stable,{3,{}},102),StateError::Conflict);
        QCOMPARE(b.accept(Channel::Stable,newer,103),StateError::None);
        QCOMPARE(a.read(Channel::Stable).value->accepted->sequence,std::uint64_t(3));
        QCOMPARE(a.read(Channel::Preview).value->accepted->sequence,std::uint64_t(9));
        QCOMPARE(a.skip(Channel::Stable,UINT64_MAX),StateError::None);
        QCOMPARE(b.accept(Channel::Stable,{UINT64_MAX,{}},104),StateError::None);
        auto state=a.read(Channel::Stable); QVERIFY(state.value);
        QCOMPARE(*state.value->skippedReleaseSequence,UINT64_MAX);
        QCOMPARE(state.value->accepted->sequence,UINT64_MAX);
    }
    void backoffAndCancellation() {
        QTemporaryDir dir; UpdateStateStore store(dir.filePath("state.json"));
        QCOMPARE(store.accept(Channel::Stable,{2,{}},100),StateError::None);
        for(qint64 delay:{900,3600,21600,86400,86400}) {
            QCOMPARE(store.recordFailure(Channel::Stable,200),StateError::None);
            const auto state=store.read(Channel::Stable);
            QVERIFY(state.value); QCOMPARE(state.value->nextAttempt,200+delay);
            QCOMPARE(state.value->accepted->sequence,std::uint64_t(2));
            QCOMPARE(state.value->lastSuccess,qint64(100));
        }
        QCOMPARE(store.recordCancellation(Channel::Stable,300),StateError::None);
        QCOMPARE(store.read(Channel::Stable).value->nextAttempt,qint64(1200));
        QCOMPARE(store.read(Channel::Stable).value->failureCount,4u);
        QCOMPARE(store.accept(Channel::Stable,{3,{}},400),StateError::None);
        QCOMPARE(store.read(Channel::Stable).value->failureCount,0u);
        QCOMPARE(store.recordFailure(Channel::Stable,INT64_MAX-10),StateError::None);
        QCOMPARE(store.read(Channel::Stable).value->nextAttempt,qint64(INT64_MAX));
        QCOMPARE(store.recordCancellation(Channel::Stable,INT64_MAX),StateError::None);
        QCOMPARE(store.read(Channel::Stable).value->nextAttempt,qint64(INT64_MAX));
        QCOMPARE(store.accept(Channel::Stable,{4,{}},INT64_MAX),StateError::None);
        QCOMPARE(store.read(Channel::Stable).value->nextAttempt,qint64(INT64_MAX));
    }
    void lockAndWriteFailurePreserveFile() {
        QTemporaryDir dir; const auto path=dir.filePath("state.json");
        UpdateStateStore store(path);
        QCOMPARE(store.accept(Channel::Stable,{2,{}},100),StateError::None);
        const auto original=bytes(path);
        QLockFile lock(path+".lock"); QVERIFY(lock.tryLock(0));
        QCOMPARE(store.accept(Channel::Stable,{3,{}},101),StateError::Busy);
        QCOMPARE(bytes(path),original);
        lock.unlock();
#ifdef Q_OS_WIN
        HANDLE held=CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),GENERIC_READ,
            FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        QVERIFY(held!=INVALID_HANDLE_VALUE);
        const auto result=store.accept(Channel::Stable,{3,{}},102);
        CloseHandle(held);
        QCOMPARE(result,StateError::WriteFailed);
        QCOMPARE(bytes(path),original);
        QCOMPARE(store.read(Channel::Stable).value->accepted->sequence,std::uint64_t(2));
#endif
        UpdateStateStore directory(dir.path());
        QVERIFY(directory.recordFailure(Channel::Stable,100)!=StateError::None);
        QVERIFY(!directory.read(Channel::Stable).value);
    }
    void invalidInputDoesNotMutate() {
        QTemporaryDir dir; const auto path=dir.filePath("state.json"); UpdateStateStore store(path);
        QCOMPARE(store.accept(Channel::Stable,{2,{}},100),StateError::None);
        const auto original=bytes(path);
        QCOMPARE(store.accept(Channel::Stable,{0,{}},100),StateError::Invalid);
        QCOMPARE(store.accept(Channel::Stable,{3,{}},-1),StateError::Invalid);
        QCOMPARE(store.recordFailure(Channel::Stable,-1),StateError::Invalid);
        QCOMPARE(store.recordCancellation(Channel::Stable,-1),StateError::Invalid);
        QCOMPARE(store.skip(Channel::Stable,0),StateError::Invalid);
        QCOMPARE(store.skip(static_cast<Channel>(99),2),StateError::Invalid);
        QCOMPARE(bytes(path),original);
        UpdateStateStore relative("must-not-create-state.json");
        QVERIFY(!relative.read(Channel::Stable).value);
        QCOMPARE(relative.recordFailure(Channel::Stable,100),StateError::Invalid);
    }
    void corruptFileIsNotReset() {
        QTemporaryDir dir; const auto path=dir.filePath("state.json"); UpdateStateStore store(path);
        QCOMPARE(store.accept(Channel::Stable,{2,{}},100),StateError::None);
        const auto valid=nlohmann::json::parse(bytes(path).toStdString());
        auto atLimit=QByteArray::fromStdString(valid.dump());
        atLimit.append(QByteArray(16*1024-atLimit.size(),' '));
        QVERIFY(write(path,atLimit));
        QVERIFY(store.read(Channel::Stable).value);
        atLimit.append(' ');
        QVERIFY(write(path,atLimit));
        QCOMPARE(store.read(Channel::Stable).error,StateError::Invalid);
        QCOMPARE(store.recordFailure(Channel::Stable,101),StateError::Invalid);
        QCOMPARE(bytes(path),atLimit);
        std::vector<QByteArray> bad{"", "{", "{}", QByteArray(16*1024+1,'x')};
        auto duplicate=QByteArray::fromStdString(valid.dump()); duplicate.insert(1,"\"schema\":1,"); bad.push_back(duplicate);
        for(const auto& field:{"schema","stable","preview"}) {
            auto j=valid; j.erase(field); bad.push_back(QByteArray::fromStdString(j.dump()));
        }
        for(const auto& pointer:{"/unexpected","/stable/unexpected","/stable/accepted/unexpected"}) {
            auto j=valid; j[nlohmann::json::json_pointer(pointer)]=0;
            bad.push_back(QByteArray::fromStdString(j.dump()));
        }
        for(const auto& item:std::vector<std::pair<std::string,nlohmann::json>>{
                {"lastSuccess",-1},{"nextAttempt",1.5},{"failureCount",5},
                {"nextAttempt",18446744073709551615ULL},{"skippedReleaseSequence","01"}}) {
            auto j=valid; j["stable"][item.first]=item.second; bad.push_back(QByteArray::fromStdString(j.dump()));
        }
        for(const auto& item:std::vector<std::pair<std::string,std::string>>{
                {"sequence","0"},{"sequence","18446744073709551616"},{"payloadDigest",std::string(128,'A')},
                {"payloadDigest","42"}}) {
            auto j=valid; j["stable"]["accepted"][item.first]=item.second;
            bad.push_back(QByteArray::fromStdString(j.dump()));
        }
        for(const auto& data:bad) {
            QVERIFY(write(path,data));
            QVERIFY(!store.read(Channel::Stable).value);
            QVERIFY(store.recordFailure(Channel::Stable,101)!=StateError::None);
            QCOMPARE(bytes(path),data);
        }
    }
    void dueBoundaries() {
        ChannelState state;
        QVERIFY(isCheckDue(state,100));
        state.nextAttempt=1000;
        QVERIFY(!isCheckDue(state,999));
        QVERIFY(isCheckDue(state,1000));
        state.nextAttempt=100+86400;
        QVERIFY(!isCheckDue(state,100));
        state.nextAttempt++;
        QVERIFY(isCheckDue(state,100));
        QVERIFY(!isCheckDue(state,-1));
        state.nextAttempt=INT64_MAX;
        QVERIFY(!isCheckDue(state,INT64_MAX-1));
        QVERIFY(isCheckDue(state,INT64_MAX));
    }
};
QTEST_GUILESS_MAIN(StateTest)
#include "updatestatetest.moc"
