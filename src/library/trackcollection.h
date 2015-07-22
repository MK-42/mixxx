// trackcollection.h
// Created 10/27/2008 by Albert Santoni <gamegod \a\t users.sf.net>
// Lambda scheme introduced 27/09/2013 by Nazar Gerasymchuk <troyan3 @ gmail.com>

#ifndef TRACKCOLLECTION_H
#define TRACKCOLLECTION_H

#include <QtSql>
#include <QList>
#include <QQueue>
#include <QRegExp>
#include <QSemaphore>
#include <QMutex>
#include <QSharedPointer>
#include <QSqlDatabase>
#include <QApplication>
#include <QThread>
#include <QAtomicInt>
#include <functional> 

#include "configobject.h"
#include "library/trackcollectionprivate.h"
#include "library/basetrackcache.h"
#include "library/dao/trackdao.h"
#include "library/dao/cratedao.h"
#include "library/dao/cuedao.h"
#include "library/dao/playlistdao.h"
#include "library/dao/analysisdao.h"
#include "library/queryutil.h"
#include "library/dao/directorydao.h"

class TrackInfoObject;

#define AUTODJ_TABLE "Auto DJ"

// Lambda function
typedef std::function <void (TrackCollectionPrivate*)> func;
typedef std::function <void ()> funcNoParams;

class TrackInfoObject;
class ControlObjectThread;
class BpmDetector;

// Helper class for calling some code in Main thread
class MainExecuter : public QObject {
    Q_OBJECT
public:
    ~MainExecuter() {}

    // singletone
    static MainExecuter& getInstance() {
        static MainExecuter instance;
        return instance;
    }

    static void callAsync(funcNoParams lambda, QString where = QString()) {
        DBG() << where;
        MainExecuter& instance = getInstance();
        instance.m_lambdaMutex.lock();
        instance.setLambda(lambda);
        emit(instance.runOnMainThread());
    }

    static void callSync(funcNoParams lambda, QString where = QString()) {
        DBG() << where;
        if (QThread::currentThread() == qApp->thread()) {
            // We are already on Main thread
            lambda();
        } else {
            MainExecuter& instance = getInstance();
            instance.m_lambdaMutex.lock();
            instance.setLambda(lambda);
            emit(instance.runOnMainThread());
            instance.m_lambdaMutex.lock();
            instance.m_lambdaMutex.unlock();
        }
    }

signals:
    void runOnMainThread();

public slots:
    void call() {
        if (m_lamdaCount.testAndSetAcquire(1, 0)) {;
            DBG() << "calling m_lambda in " << QThread::currentThread()->objectName();
            m_lambda();
            m_lambdaMutex.unlock();
        }
    }

private:
    MainExecuter()
            : m_lambda(NULL) {
        moveToThread(qApp->thread());
        connect(this, SIGNAL(runOnMainThread()),
                this, SLOT(call()), Qt::QueuedConnection);
    }

    void setLambda(funcNoParams newLambda) {
        m_lambda = newLambda;
        m_lamdaCount = 1;
    }

    QMutex m_lambdaMutex;
    funcNoParams m_lambda;
    QAtomicInt m_lamdaCount;
};


// Separate thread providing database access. Holds all DAO. To make access to DB
// see callAsync/callSync methods.
class TrackCollection : public QThread {
    Q_OBJECT
  public:
    TrackCollection(ConfigObject<ConfigValue>* pConfig);
    ~TrackCollection();
    void run();

    void callAsync(func lambda, QString where = QString());
    void callSync(func lambda, QString where = QString());

    void stopThread();
    void addLambdaToQueue(func lambda);

    void setupControlObject();
    void setUiEnabled(const bool enabled);

    QSharedPointer<BaseTrackCache> getTrackSource(const QString& name);
    void addTrackSource(const QString& name, QSharedPointer<BaseTrackCache> trackSource);

    ConfigObject<ConfigValue>* getConfig();

  signals:
    void initialized();

  private:
    TrackCollectionPrivate* m_pTrackCollectionPrivate;
    ConfigObject<ConfigValue>* m_pConfig;
    QHash<QString, QSharedPointer<BaseTrackCache> > m_trackSources;
    QQueue<func> m_lambdas;
    volatile bool m_stop;
    QMutex m_lambdasQueueMutex; // mutex for accessing queue of lambdas
    QSemaphore m_semLambdasReadyToCall; // lambdas in queue ready to call
    QSemaphore m_semLambdasFree; // count of vacant places for lambdas in queue
    ControlObjectThread* m_pCOTPlaylistIsBusy;

    int m_inCallSyncCount;
};

#endif
