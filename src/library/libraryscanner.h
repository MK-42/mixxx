
/***************************************************************************
                          libraryscanner.h  -  scans library in a thread
                             -------------------
    begin                : 11/27/2007
    copyright            : (C) 2007 Albert Santoni
    email                : gamegod \a\t users.sf.net
***************************************************************************/

/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#ifndef LIBRARYSCANNER_H
#define LIBRARYSCANNER_H

#include <QThread>
#include <QList>
#include <QString>
#include <QList>
#include <QWidget>
#include <QSqlDatabase>
#include <QStringList>
#include <QRegExp>
#include <QFileInfo>
#include <QLinkedList>

#include "library/dao/cratedao.h"
#include "library/dao/cuedao.h"
#include "library/dao/libraryhashdao.h"
#include "library/dao/directorydao.h"
#include "library/dao/playlistdao.h"
#include "library/dao/trackdao.h"
#include "library/dao/analysisdao.h"
#include "libraryscannerdlg.h"
#include "trackcollection.h"

class TrackInfoObject;

class LibraryScanner : public QThread {
    Q_OBJECT
  public:
    LibraryScanner();
    LibraryScanner(TrackCollection* pTrackCollection);
    virtual ~LibraryScanner();

    void run();
    void scan(QWidget* parent);

    void scan();
    void addTrackToChunk(const QString filePath);
    void addChunkToDatabase();

    QMutex m_pauseMutex;
  public slots:
    void cancel();
    void resetCancel();
    void pause();
    void resume();
    void updateProgress();
  signals:
    void startedLoading();
    void finishedLoading();
    void scanFinished();
    void progressHashing(QString);
    void progressLoading(QString path);

  private:
    // Recursively scan a music library. Doesn't import tracks for any
    // directories that have already been scanned and have not changed. Changes
    // are tracked by performing a hash of the directory's file list, and those
    // hashes are stored in the database.
    bool recursiveScan(const QDir& dir, QStringList& verifiedDirectories);

    bool importDirectory(const QString& directory, volatile bool* cancel);

    // The library trackcollection
    TrackCollection* m_pTrackCollection;
    // Hang on to a different DB connection since we run in a different thread
    QSqlDatabase m_database;
    // The library scanning window
    LibraryScannerDlg* m_pProgress;
    LibraryHashDAO m_libraryHashDao;
    CueDAO m_cueDao;
    PlaylistDAO m_playlistDao;
    CrateDAO m_crateDao;
    DirectoryDAO m_directoryDao;
    AnalysisDao m_analysisDao;
    TrackDAO m_trackDao;
    QRegExp m_extensionFilter;
    volatile bool m_bCancelLibraryScan;
    QStringList m_directoriesBlacklist;
    QStringList m_tracksListInCnunk;
    QTimer m_timer;
    QString m_tmpTrackPath;
};

#endif
