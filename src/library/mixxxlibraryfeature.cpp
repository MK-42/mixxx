// mixxxlibraryfeature.cpp
// Created 8/23/2009 by RJ Ryan (rryan@mit.edu)

#include <QtDebug>

#include "library/mixxxlibraryfeature.h"

#include "library/basetrackcache.h"
#include "library/librarytablemodel.h"
#include "library/missingtablemodel.h"
#include "library/hiddentablemodel.h"
#include "library/queryutil.h"
#include "library/trackcollection.h"
#include "treeitem.h"
#include "soundsourceproxy.h"
#include "widget/wlibrary.h"
#include "util/dnd.h"

MixxxLibraryFeature::MixxxLibraryFeature(QObject* parent,
                                         TrackCollection* pTrackCollection,
                                         ConfigObject<ConfigValue>* pConfig)
        : LibraryFeature(parent),
          kMissingTitle(tr("Missing Tracks")),
          kHiddenTitle(tr("Hidden Tracks")),
          m_pLibraryTableModel(new LibraryTableModel(this, pTrackCollection, "mixxx.db.model.library")),
          m_pMissingView(NULL),
          m_pHiddenView(NULL),
          m_pConfig(pConfig),
          m_pTrackCollection(pTrackCollection) {
}

void MixxxLibraryFeature::init() {
    QString COVERART_LOCATION = COVERART_TABLE + "." + COVERARTTABLE_LOCATION
                                + " AS " + LIBRARYTABLE_COVERART_LOCATION;
    QStringList columns = QStringList()
            << "library." + LIBRARYTABLE_ID
            << "library." + LIBRARYTABLE_PLAYED
            << "library." + LIBRARYTABLE_TIMESPLAYED
               //has to be up here otherwise Played and TimesPlayed are not show
            << "library." + LIBRARYTABLE_ARTIST
            << "library." + LIBRARYTABLE_TITLE
            << "library." + LIBRARYTABLE_ALBUM
            << "library." + LIBRARYTABLE_ALBUMARTIST
            << "library." + LIBRARYTABLE_YEAR
            << "library." + LIBRARYTABLE_DURATION
            << "library." + LIBRARYTABLE_RATING
            << "library." + LIBRARYTABLE_GENRE
            << "library." + LIBRARYTABLE_COMPOSER
            << "library." + LIBRARYTABLE_GROUPING
            << "library." + LIBRARYTABLE_FILETYPE
            << "library." + LIBRARYTABLE_TRACKNUMBER
            << "library." + LIBRARYTABLE_KEY
            << "library." + LIBRARYTABLE_KEY_ID
            << "library." + LIBRARYTABLE_DATETIMEADDED
            << "library." + LIBRARYTABLE_BPM
            << "library." + LIBRARYTABLE_BPM_LOCK
            << "library." + LIBRARYTABLE_BITRATE
            << "track_locations.location"
            << "track_locations.fs_deleted"
            << "library." + LIBRARYTABLE_COMMENT
            << "library." + LIBRARYTABLE_MIXXXDELETED
            << COVERART_LOCATION
            << COVERART_TABLE + "." + COVERARTTABLE_MD5;

    QString tableName = "library_cache_view";

    // tro's lambda idea. This code calls asynchronously!
    m_pTrackCollection->callSync(
                [this, columns, tableName] (TrackCollectionPrivate* pTrackCollectionPrivate) {
        QSqlQuery query(pTrackCollectionPrivate->getDatabase());
        QString queryString = QString(
                    "CREATE TEMPORARY VIEW IF NOT EXISTS %1 AS "
                    "SELECT %2 FROM library "
                    "INNER JOIN track_locations ON library.location = track_locations.id "
                    "LEFT JOIN cover_art ON library.cover_art = cover_art.id")
                .arg(tableName, columns.join(","));
        query.prepare(queryString);
        if (!query.exec()) {
            LOG_FAILED_QUERY(query);
        }
    }, __PRETTY_FUNCTION__);

    // Strip out library. track_locations. and cover_art.
    for (QStringList::iterator it = columns.begin();
         it != columns.end(); ++it) {
        if (it->startsWith("library.")) {
            *it = it->replace("library.", "");
        } else if (it->startsWith("track_locations.")) {
            *it = it->replace("track_locations.", "");
        } else if (it->operator==(COVERART_LOCATION)) {
            *it = LIBRARYTABLE_COVERART_LOCATION;
        } else if (it->startsWith(COVERART_TABLE + ".")) {
            *it = it->replace(COVERART_TABLE + ".", "");
        }
    }

    BaseTrackCache* pBaseTrackCache = new BaseTrackCache(
        m_pTrackCollection, tableName, LIBRARYTABLE_ID, columns, true);

    // All database access must be provided thru TrackCollection thread,
    //    According to it, all signals emitted from TrackDAO will be emitted
    //    from TrackCollection thread. Qt::DirectConnection uses here
    m_pTrackCollection->callSync( [this, &pBaseTrackCache] (TrackCollectionPrivate* pTrackCollectionPrivate){
        connect(&pTrackCollectionPrivate->getTrackDAO(), SIGNAL(trackDirty(int)),
                pBaseTrackCache, SLOT(slotTrackDirty(int)),
                Qt::DirectConnection);
        connect(&pTrackCollectionPrivate->getTrackDAO(), SIGNAL(trackClean(int)),
                pBaseTrackCache, SLOT(slotTrackClean(int)),
                Qt::DirectConnection);
        connect(&pTrackCollectionPrivate->getTrackDAO(), SIGNAL(trackChanged(int)),
                pBaseTrackCache, SLOT(slotTrackChanged(int)),
                Qt::DirectConnection);
        connect(&pTrackCollectionPrivate->getTrackDAO(), SIGNAL(tracksAdded(QSet<int>)),
                pBaseTrackCache, SLOT(slotTracksAdded(QSet<int>)),
                Qt::DirectConnection);
        connect(&pTrackCollectionPrivate->getTrackDAO(), SIGNAL(tracksRemoved(QSet<int>)),
                pBaseTrackCache, SLOT(slotTracksRemoved(QSet<int>)),
                Qt::DirectConnection);
        connect(&pTrackCollectionPrivate->getTrackDAO(), SIGNAL(dbTrackAdded(TrackPointer)),
                pBaseTrackCache, SLOT(slotDbTrackAdded(TrackPointer)),
                Qt::DirectConnection);
        connect(&pTrackCollectionPrivate->getTrackDAO(), SIGNAL(updateTrackInBTC(int)),
                pBaseTrackCache, SLOT(slotUpdateTrack(int)),
                Qt::DirectConnection);
        connect(&pTrackCollectionPrivate->getTrackDAO(), SIGNAL(updateTracksInBTC(QSet<int>)),
                pBaseTrackCache, SLOT(slotUpdateTracks(QSet<int>)),
                Qt::DirectConnection);
    }, __PRETTY_FUNCTION__);
    m_pBaseTrackCache = QSharedPointer<BaseTrackCache>(pBaseTrackCache);
    //m_pTrackCollection->addTrackSource(QString("default"), m_pBaseTrackCache);
    // These rely on the 'default' track source being present.
    m_pTrackCollection->setTrackSource(m_pBaseTrackCache);

    // NOTE(tro) Moved next commented line to c-tor initialization list
    // m_pLibraryTableModel = new LibraryTableModel(this, m_pTrackCollection);

    m_pLibraryTableModel->init();
}

// NOTE(tro) Moved to separate method, since we cannot create children for
//           a parent that is in a different thread
void MixxxLibraryFeature::initUI() {
    TreeItem* pRootItem = new TreeItem();
    TreeItem* pmissingChildItem = new TreeItem(kMissingTitle, kMissingTitle,
                                       this, pRootItem);
    TreeItem* phiddenChildItem = new TreeItem(kHiddenTitle, kHiddenTitle,
                                       this, pRootItem);
    pRootItem->appendChild(pmissingChildItem);
    pRootItem->appendChild(phiddenChildItem);

    m_childModel.setRootItem(pRootItem);
}

MixxxLibraryFeature::~MixxxLibraryFeature() {
    delete m_pLibraryTableModel;
}

void MixxxLibraryFeature::bindWidget(WLibrary* pLibrary,
                    MixxxKeyboard* pKeyboard) {
    m_pHiddenView = new DlgHidden(pLibrary,
                                  m_pConfig, m_pTrackCollection,
                                  pKeyboard);
    m_pHiddenView->init();

    pLibrary->registerView(kHiddenTitle, m_pHiddenView);
    connect(m_pHiddenView, SIGNAL(loadCoverArt(const QString&, const QString&, int)),
            this, SIGNAL(loadCoverArt(const QString&, const QString&, int)));

    m_pMissingView = new DlgMissing(pLibrary,
                                  m_pConfig, m_pTrackCollection,
                                  pKeyboard);
    pLibrary->registerView(kMissingTitle, m_pMissingView);
    connect(m_pMissingView, SIGNAL(loadCoverArt(const QString&, const QString&, int)),
            this, SIGNAL(loadCoverArt(const QString&, const QString&, int)));
}

QVariant MixxxLibraryFeature::title() {
    return tr("Library");
}

QIcon MixxxLibraryFeature::getIcon() {
    return QIcon(":/images/library/ic_library_library.png");
}

TreeItemModel* MixxxLibraryFeature::getChildModel() {
    return &m_childModel;
}

void MixxxLibraryFeature::refreshLibraryModels() {
    if (m_pLibraryTableModel) {
        // tro's lambda idea. This code calls synchronously!
        m_pTrackCollection->callSync(
                [this] (TrackCollectionPrivate* pTrackCollectionPrivate) {
            m_pLibraryTableModel->select(pTrackCollectionPrivate);
        }, __PRETTY_FUNCTION__);
    }
    if (m_pMissingView) {
        m_pMissingView->onShow();
    }
    if (m_pHiddenView) {
        m_pHiddenView->onShow();
    }
}

void MixxxLibraryFeature::activate() {
    qDebug() << "MixxxLibraryFeature::activate()";
    emit(showTrackModel(m_pLibraryTableModel));
    emit(enableCoverArtDisplay(true));
}

void MixxxLibraryFeature::activateChild(const QModelIndex& index) {
    QString itemName = index.data().toString();
    emit(switchToView(itemName));
    emit(enableCoverArtDisplay(true));
}

// Must be called from Main thread
bool MixxxLibraryFeature::dropAccept(QList<QUrl> urls, QObject* pSource) {
    if (pSource) {
        return false;
    } else {
        QList<QFileInfo> files = DragAndDropHelper::supportedTracksFromUrls(urls, false, true);
        bool result = false;
        // tro's lambda idea. This code calls synchronously!
        m_pTrackCollection->callSync(
                [this, &files, &result] (TrackCollectionPrivate* pTrackCollectionPrivate) {
            // Adds track, does not insert duplicates, handles unremoving logic.
            QList<int> trackIds = pTrackCollectionPrivate->getTrackDAO().addTracks(files, true);
            result = trackIds.size() > 0;
        }, __PRETTY_FUNCTION__);
        return result;
    }
}

bool MixxxLibraryFeature::dragMoveAccept(QUrl url) {
    QFileInfo file(url.toLocalFile());
    return SoundSourceProxy::isFilenameSupported(file.fileName());
}
