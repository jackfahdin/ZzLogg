#include "signalthrottler.h"

#include <QCoreApplication>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

class SignalThrottlerTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void idleDoesNotEmit()
    {
        SignalThrottler throttler;
        throttler.setTimeout( 50 );
        QSignalSpy triggered( &throttler, &SignalThrottler::triggered );
        QTest::qWait( 150 );
        QCOMPARE( triggered.count(), 0 );
    }

    void emitsAtTrailingEdgeAndStops_data()
    {
        QTest::addColumn<int>( "requests" );
        QTest::newRow( "single" ) << 1;
        QTest::newRow( "burst" ) << 100;
    }

    void emitsAtTrailingEdgeAndStops()
    {
        QFETCH( int, requests );
        SignalThrottler throttler;
        throttler.setTimeout( 50 );
        QSignalSpy triggered( &throttler, &SignalThrottler::triggered );
        for ( int i = 0; i < requests; ++i ) throttler.throttle();
        QCOMPARE( triggered.count(), 0 );
        QTRY_COMPARE_WITH_TIMEOUT( triggered.count(), 1, 3000 );
        QTest::qWait( 150 );
        QCOMPARE( triggered.count(), 1 );
    }

    void continuousInputIsNotDebounced()
    {
        SignalThrottler throttler;
        throttler.setTimeout( 80 );
        QSignalSpy triggered( &throttler, &SignalThrottler::triggered );
        QTimer input;
        input.setInterval( 5 );
        connect( &input, &QTimer::timeout, &throttler, &SignalThrottler::throttle );
        input.start();
        QTRY_VERIFY_WITH_TIMEOUT( triggered.count() >= 3, 5000 );
        QVERIFY( input.isActive() );
        input.stop();
    }

    void reentrantRequestStartsNextWindow()
    {
        SignalThrottler throttler;
        throttler.setTimeout( 50 );
        int count = 0;
        connect( &throttler, &SignalThrottler::triggered, this, [&] {
            if ( ++count == 1 ) throttler.throttle();
        } );
        throttler.throttle();
        QTRY_COMPARE_WITH_TIMEOUT( count, 2, 3000 );
        QTest::qWait( 150 );
        QCOMPARE( count, 2 );
    }

    void deliversLatestAndFinalProgress()
    {
        SignalThrottler throttler;
        throttler.setTimeout( 50 );
        int latest = 0;
        QList<int> delivered;
        connect( &throttler, &SignalThrottler::triggered, this,
                 [&] { delivered.append( latest ); } );
        for ( int progress = 0; progress <= 75; ++progress ) {
            latest = progress;
            throttler.throttle();
        }
        QTRY_COMPARE_WITH_TIMEOUT( delivered, QList<int>{ 75 }, 3000 );
        latest = 100;
        throttler.throttle();
        QTRY_COMPARE_WITH_TIMEOUT( delivered.size(), 2, 3000 );
        QCOMPARE( delivered.back(), 100 );
    }

    void destructionFlushesOnlyPendingRequest_data()
    {
        QTest::addColumn<bool>( "pending" );
        QTest::newRow( "idle" ) << false;
        QTest::newRow( "pending" ) << true;
    }

    void destructionFlushesOnlyPendingRequest()
    {
        QFETCH( bool, pending );
        int count = 0;
        {
            SignalThrottler throttler;
            throttler.setTimeout( 50 );
            connect( &throttler, &SignalThrottler::triggered, this, [&] { ++count; } );
            if ( pending ) throttler.throttle();
            QCOMPARE( count, 0 );
        }
        QCOMPARE( count, pending ? 1 : 0 );
        QTest::qWait( 150 );
        QCOMPARE( count, pending ? 1 : 0 );
    }

    void destroyingReceiverCancelsItsCallback()
    {
        SignalThrottler throttler;
        throttler.setTimeout( 50 );
        QSignalSpy triggered( &throttler, &SignalThrottler::triggered );
        int calls = 0;
        {
            QObject receiver;
            connect( &throttler, &SignalThrottler::triggered, &receiver, [&] { ++calls; } );
            throttler.throttle();
        }
        QTRY_COMPARE_WITH_TIMEOUT( triggered.count(), 1, 3000 );
        QCOMPARE( calls, 0 );
    }

    void canBeDeletedFromTriggeredCallback()
    {
        QPointer<SignalThrottler> throttler = new SignalThrottler;
        throttler->setTimeout( 50 );
        int count = 0;
        connect( throttler, &SignalThrottler::triggered, this, [&] {
            ++count;
            delete throttler.data();
        } );
        throttler->throttle();
        QTRY_VERIFY_WITH_TIMEOUT( throttler.isNull(), 3000 );
        QTest::qWait( 150 );
        QCOMPARE( count, 1 );
    }
};

QTEST_GUILESS_MAIN( SignalThrottlerTest )
#include "signalthrottlertest.moc"
