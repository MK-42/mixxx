#include "library/baseplaylistfeature.h"

#include <QInputDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QDesktopServices>

#include "library/parser.h"
#include "library/parserm3u.h"
#include "library/parserpls.h"
#include "library/parsercsv.h"
#include "library/playlisttablemodel.h"
#include "library/trackcollection.h"
#include "library/treeitem.h"
#include "mixxxkeyboard.h"
#include "widget/wlibrary.h"
#include "widget/wlibrarytextbrowser.h"

BasePlaylistFeature::BasePlaylistFeature(QObject* parent,
                                         ConfigObject<ConfigValue>* pConfig,
                                         TrackCollection* pTrackCollection,
                                         QString rootViewName)
        : LibraryFeature(parent),
          m_pConfig(pConfig),
          m_pTrackCollection(pTrackCollection),
          m_pPlaylistTableModel(NULL),
          m_rootViewName(rootViewName) {
    m_pCreatePlaylistAction = new QAction(tr("Create New Playlist"),this);
    connect(m_pCreatePlaylistAction, SIGNAL(triggered()),
            this, SLOT(slotCreatePlaylist()));

    m_pAddToAutoDJAction = new QAction(tr("Add to Auto DJ Queue (bottom)"), this);
    connect(m_pAddToAutoDJAction, SIGNAL(triggered()),
            this, SLOT(slotAddToAutoDJ()));

    m_pAddToAutoDJTopAction = new QAction(tr("Add to Auto DJ Queue (top)"), this);
    connect(m_pAddToAutoDJTopAction, SIGNAL(triggered()),
            this, SLOT(slotAddToAutoDJTop()));

    m_pDeletePlaylistAction = new QAction(tr("Remove"),this);
    connect(m_pDeletePlaylistAction, SIGNAL(triggered()),
            this, SLOT(slotDeletePlaylist()));

    m_pRenamePlaylistAction = new QAction(tr("Rename"),this);
    connect(m_pRenamePlaylistAction, SIGNAL(triggered()),
            this, SLOT(slotRenamePlaylist()));

    m_pLockPlaylistAction = new QAction(tr("Lock"),this);
    connect(m_pLockPlaylistAction, SIGNAL(triggered()),
            this, SLOT(slotTogglePlaylistLock()));

    m_pDuplicatePlaylistAction = new QAction(tr("Duplicate"), this);
    connect(m_pDuplicatePlaylistAction, SIGNAL(triggered()),
            this, SLOT(slotDuplicatePlaylist()));

    m_pImportPlaylistAction = new QAction(tr("Import Playlist"),this);
    connect(m_pImportPlaylistAction, SIGNAL(triggered()),
            this, SLOT(slotImportPlaylist()));

    m_pExportPlaylistAction = new QAction(tr("Export Playlist"), this);
    connect(m_pExportPlaylistAction, SIGNAL(triggered()),
            this, SLOT(slotExportPlaylist()));

    m_pAnalyzePlaylistAction = new QAction(tr("Analyze entire Playlist"), this);
    connect(m_pAnalyzePlaylistAction, SIGNAL(triggered()),
            this, SLOT(slotAnalyzePlaylist()));

    pTrackCollection->callSync([this] (TrackCollectionPrivate* pTrackCollectionPrivate){
        connect(&pTrackCollectionPrivate->getPlaylistDAO(), SIGNAL(added(int)),
                this, SLOT(slotPlaylistTableChanged(int)),
                Qt::QueuedConnection);

        connect(&pTrackCollectionPrivate->getPlaylistDAO(), SIGNAL(deleted(int)),
                this, SLOT(slotPlaylistTableChanged(int)),
                Qt::QueuedConnection);

        connect(&pTrackCollectionPrivate->getPlaylistDAO(), SIGNAL(renamed(int,QString)),
                this, SLOT(slotPlaylistTableRenamed(int,QString)),
                Qt::QueuedConnection);

        connect(&pTrackCollectionPrivate->getPlaylistDAO(), SIGNAL(lockChanged(int)),
                this, SLOT(slotPlaylistTableChanged(int)),
                Qt::QueuedConnection);

        connect(&pTrackCollectionPrivate->getPlaylistDAO(), SIGNAL(changed(int)),
                this, SLOT(slotPlaylistTableChanged(int)),
                Qt::QueuedConnection);

        }, __PRETTY_FUNCTION__);

    connect(this, SIGNAL(playlistTableChanged(int)),
            this, SLOT(slotPlaylistTableChanged(int)),
            Qt::QueuedConnection);
}

BasePlaylistFeature::~BasePlaylistFeature() {
    delete m_pPlaylistTableModel;
    delete m_pCreatePlaylistAction;
    delete m_pDeletePlaylistAction;
    delete m_pImportPlaylistAction;
    delete m_pExportPlaylistAction;
    delete m_pDuplicatePlaylistAction;
    delete m_pAddToAutoDJAction;
    delete m_pAddToAutoDJTopAction;
    delete m_pRenamePlaylistAction;
    delete m_pLockPlaylistAction;
    delete m_pAnalyzePlaylistAction;
}

int BasePlaylistFeature::playlistIdFromIndex(QModelIndex index) {
    TreeItem* item = static_cast<TreeItem*>(index.internalPointer());
    if (item == NULL) {
        return -1;
    }

    QString dataPath = item->dataPath().toString();
    bool ok = false;
    int playlistId = dataPath.toInt(&ok);
    if (!ok) {
        return -1;
    }
    return playlistId;
}

void BasePlaylistFeature::activate() {
    emit(switchToView(m_rootViewName));
    emit(restoreSearch(QString())); // Null String disables search box
    emit(enableCoverArtDisplay(true));
}

void BasePlaylistFeature::activateChild(const QModelIndex& index) {
    //qDebug() << "BasePlaylistFeature::activateChild()" << index;
    int playlistId = playlistIdFromIndex(index);
    if (playlistId != -1 && m_pPlaylistTableModel) {
        m_pPlaylistTableModel->setTableModel(playlistId);
    }
    if (m_pPlaylistTableModel) {
        emit(showTrackModel(m_pPlaylistTableModel));
    }
    emit(enableCoverArtDisplay(true));
}

void BasePlaylistFeature::slotRenamePlaylist() {
    int playlistId = playlistIdFromIndex(m_lastRightClickedIndex);
    if (playlistId == -1) {
        return;
    }
    bool locked = false;
    QString oldName = "";
    // tro's lambda idea. This code calls synchronously!
    m_pTrackCollection->callSync(
            [this, &oldName, &playlistId, &locked] (TrackCollectionPrivate* pTrackCollectionPrivate) {
        oldName = pTrackCollectionPrivate->getPlaylistDAO().getPlaylistName(playlistId);
        locked = pTrackCollectionPrivate->getPlaylistDAO().isPlaylistLocked(playlistId);
    }, __PRETTY_FUNCTION__);

    if (locked) {
        qDebug() << "Skipping playlist rename because playlist" << playlistId
                 << "is locked.";
        return;
    }
    QString newName;
    bool validNameGiven = false;

    while (!validNameGiven) {
        bool ok = false;
        newName = QInputDialog::getText(NULL,
                                        tr("Rename Playlist"),
                                        tr("Enter new name for playlist:"),
                                        QLineEdit::Normal,
                                        oldName,
                                        &ok).trimmed();
        if (!ok || oldName == newName) {
            return;
        }

        int existingId = -1;
        // tro's lambda idea. This code calls synchronously!
        m_pTrackCollection->callSync(
                    [this, &newName, &existingId ] (TrackCollectionPrivate* pTrackCollectionPrivate) {
            existingId = pTrackCollectionPrivate->getPlaylistDAO().getPlaylistIdFromName(newName);
        }, __PRETTY_FUNCTION__);

        if (existingId != -1) {
            QMessageBox::warning(NULL,
                                tr("Renaming Playlist Failed"),
                                tr("A playlist by that name already exists."));
        } else if (newName.isEmpty()) {
            QMessageBox::warning(NULL,
                                tr("Renaming Playlist Failed"),
                                tr("A playlist cannot have a blank name."));
        } else {
            validNameGiven = true;
        }
    }
    // tro's lambda idea. This code calls Asynchronously!
    m_pTrackCollection->callAsync(
                [this, &playlistId, &newName] (TrackCollectionPrivate* pTrackCollectionPrivate) {
        pTrackCollectionPrivate->getPlaylistDAO().renamePlaylist(playlistId, newName);
    }, __PRETTY_FUNCTION__);
}

void BasePlaylistFeature::slotPlaylistTableRenamed(int playlistId,
                                                   QString /* a_strName */) {
    //slotPlaylistTableChanged(playlistId);
    emit playlistTableChanged(playlistId);
}

void BasePlaylistFeature::slotDuplicatePlaylist() {
    int oldPlaylistId = playlistIdFromIndex(m_lastRightClickedIndex);
    if (oldPlaylistId == -1) {
        return;
    }
    QString oldName = "";
    // tro's lambda idea. This code calls synchronously!
    m_pTrackCollection->callSync(
                [this, &oldName, &oldPlaylistId] (TrackCollectionPrivate* pTrackCollectionPrivate) {
        oldName = pTrackCollectionPrivate->getPlaylistDAO().getPlaylistName(oldPlaylistId);
    },  __PRETTY_FUNCTION__);

    QString name;
    bool validNameGiven = false;

    while (!validNameGiven) {
        bool ok = false;
        name = QInputDialog::getText(NULL,
                                     tr("Duplicate Playlist"),
                                     tr("Enter name for new playlist:"),
                                     QLineEdit::Normal,
                                     //: Appendix to default name when duplicating a playlist
                                     oldName + tr("_copy" , "[noun]"),
                                     &ok).trimmed();
        if (!ok || oldName == name) {
            return;
        }

        int existingId = -1;
        // tro's lambda idea. This code calls synchronously!
        m_pTrackCollection->callSync(
                [this, &name, &existingId] (TrackCollectionPrivate* pTrackCollectionPrivate) {
            existingId = pTrackCollectionPrivate->getPlaylistDAO().getPlaylistIdFromName(name);
        }, __PRETTY_FUNCTION__);

        if (existingId != -1) {
            QMessageBox::warning(NULL,
                                 tr("Playlist Creation Failed"),
                                 tr("A playlist by that name already exists."));
        } else if (name.isEmpty()) {
            QMessageBox::warning(NULL,
                                 tr("Playlist Creation Failed"),
                                 tr("A playlist cannot have a blank name."));
        } else {
            validNameGiven = true;
        }
    }

    // tro's lambda idea. This code calls Asynchronously!
    m_pTrackCollection->callAsync(
            [this, name, oldPlaylistId] (TrackCollectionPrivate* pTrackCollectionPrivate) {
        int newPlaylistId = pTrackCollectionPrivate->getPlaylistDAO().createPlaylist(name);
        bool copiedPlaylistTracks = pTrackCollectionPrivate->getPlaylistDAO().copyPlaylistTracks(oldPlaylistId, newPlaylistId);

        if (newPlaylistId != -1 && copiedPlaylistTracks) {
            emit(showTrackModel(m_pPlaylistTableModel));
        }
    }, __PRETTY_FUNCTION__);
}

void BasePlaylistFeature::slotTogglePlaylistLock() {
    int playlistId = playlistIdFromIndex(m_lastRightClickedIndex);
    if (playlistId == -1) {
        return;
    }
    // tro's lambda idea. This code calls asynchronously!
    m_pTrackCollection->callAsync(
                [this, playlistId] (TrackCollectionPrivate* pTrackCollectionPrivate) {
        bool locked = !pTrackCollectionPrivate->getPlaylistDAO().isPlaylistLocked(playlistId);

        if (!pTrackCollectionPrivate->getPlaylistDAO().setPlaylistLocked(playlistId, locked)) {
            qDebug() << "Failed to toggle lock of playlistId " << playlistId;
        }
    }, __PRETTY_FUNCTION__);
}

void BasePlaylistFeature::slotCreatePlaylist() {
    if (!m_pPlaylistTableModel) {
        return;
    }

    QString name;
    bool validNameGiven = false;

    while (!validNameGiven) {
        bool ok = false;
        name = QInputDialog::getText(NULL,
                                     tr("Create New Playlist"),
                                     tr("Enter name for new playlist:"),
                                     QLineEdit::Normal,
                                     tr("New Playlist"),
                                     &ok).trimmed();
        if (!ok)
            return;

        int existingId = -1;
        // tro's lambda idea. This code calls synchronously!
        m_pTrackCollection->callSync(
                [this, &name, &existingId] (TrackCollectionPrivate* pTrackCollectionPrivate) {
             existingId = pTrackCollectionPrivate->getPlaylistDAO().getPlaylistIdFromName(name);
        }, __PRETTY_FUNCTION__);

        if (existingId != -1) {
            QMessageBox::warning(NULL,
                                 tr("Playlist Creation Failed"),
                                 tr("A playlist by that name already exists."));
        } else if (name.isEmpty()) {
            QMessageBox::warning(NULL,
                                 tr("Playlist Creation Failed"),
                                 tr("A playlist cannot have a blank name."));
        } else {
            validNameGiven = true;
        }
    }

    int playlistId = -1;
    // tro's lambda idea. This code calls synchronously!
    m_pTrackCollection->callSync(
                [this, &name, &playlistId] (TrackCollectionPrivate* pTrackCollectionPrivate) {
        playlistId = pTrackCollectionPrivate->getPlaylistDAO().createPlaylist(name);
    }, __PRETTY_FUNCTION__);

    if (playlistId != -1) {
        emit(showTrackModel(m_pPlaylistTableModel));
    } else {
        QMessageBox::warning(NULL,
                             tr("Playlist Creation Failed"),
                             tr("An unknown error occurred while creating playlist: ")
                              + name);
    }
}

void BasePlaylistFeature::slotDeletePlaylist() {
    //qDebug() << "slotDeletePlaylist() row:" << m_lastRightClickedIndex.data();
    int playlistId = playlistIdFromIndex(m_lastRightClickedIndex);
    if (playlistId == -1) {
        return;
    }

    // tro's lambda idea. This code calls synchronously!
    m_pTrackCollection->callSync(
                [this, &playlistId] (TrackCollectionPrivate* pTrackCollectionPrivate) {
        //qDebug() << "slotDeletePlaylist() row:" << m_lastRightClickedIndex.data();
        bool locked = pTrackCollectionPrivate->getPlaylistDAO().isPlaylistLocked(playlistId);
        if (locked) {
            qDebug() << "Skipping playlist deletion because playlist" << playlistId << "is locked.";
            return;
        }
        if (m_lastRightClickedIndex.isValid()) {
            Q_ASSERT(playlistId >= 0);
            pTrackCollectionPrivate->getPlaylistDAO().deletePlaylist(playlistId);
        }
    }, __PRETTY_FUNCTION__);
    activate();
}

// Must be called from Main thread
void BasePlaylistFeature::slotImportPlaylist() {
    qDebug() << "slotImportPlaylist() row:" ; //<< m_lastRightClickedIndex.data();

    if (!m_pPlaylistTableModel) {
        return;
    }

    QString lastPlaylistDirectory = m_pConfig->getValueString(
            ConfigKey("[Library]", "LastImportExportPlaylistDirectory"),
            QDesktopServices::storageLocation(QDesktopServices::MusicLocation));

    QString playlist_file = QFileDialog::getOpenFileName(
            NULL,
            tr("Import Playlist"),
            lastPlaylistDirectory,
            tr("Playlist Files (*.m3u *.m3u8 *.pls *.csv)"));
    // Exit method if user cancelled the open dialog.
    if (playlist_file.isNull() || playlist_file.isEmpty()) {
        return;
    }

    // Update the import/export playlist directory
    QFileInfo fileName(playlist_file);
    m_pConfig->set(ConfigKey("[Library]","LastImportExportPlaylistDirectory"),
                ConfigValue(fileName.dir().absolutePath()));

    // The user has picked a new directory via a file dialog. This means the
    // system sandboxer (if we are sandboxed) has granted us permission to this
    // folder. We don't need access to this file on a regular basis so we do not
    // register a security bookmark.

    Parser* playlist_parser = NULL;

    if (playlist_file.endsWith(".m3u", Qt::CaseInsensitive) ||
            playlist_file.endsWith(".m3u8", Qt::CaseInsensitive)) {
        playlist_parser = new ParserM3u();
    } else if (playlist_file.endsWith(".pls", Qt::CaseInsensitive)) {
        playlist_parser = new ParserPls();
    } else if (playlist_file.endsWith(".csv", Qt::CaseInsensitive)) {
        playlist_parser = new ParserCsv();
    } else {
        return;
    }
    QList<QString> entries = playlist_parser->parse(playlist_file);

    // delete the parser object
    if (playlist_parser) {
        delete playlist_parser;
    }

    // tro's lambda idea. This code calls asynchronously!
    m_pTrackCollection->callAsync(
            [this, entries] (TrackCollectionPrivate* pTrackCollectionPrivate) {
                            Q_UNUSED(pTrackCollectionPrivate);
        // Iterate over the List that holds URLs of playlist entires
        m_pPlaylistTableModel->addTracks(QModelIndex(), entries);
    }, __PRETTY_FUNCTION__);
}

void BasePlaylistFeature::slotExportPlaylist() {
    if (!m_pPlaylistTableModel) {
        return;
    }
    int playlistId = playlistIdFromIndex(m_lastRightClickedIndex);
    if (playlistId == -1) {
        return;
    }
    QString playlistName = "";
    // tro's lambda idea. This code calls asynchronously!
    m_pTrackCollection->callSync(
            [this, &playlistName, &playlistId](TrackCollectionPrivate* pTrackCollectionPrivate) {
        playlistName = pTrackCollectionPrivate->getPlaylistDAO().getPlaylistName(playlistId);
    }, __PRETTY_FUNCTION__);
    qDebug() << "Export playlist" << playlistName;

    QString lastPlaylistDirectory = m_pConfig->getValueString(
                ConfigKey("[Library]", "LastImportExportPlaylistDirectory"),
                QDesktopServices::storageLocation(QDesktopServices::MusicLocation));

    // Open a dialog to let the user choose the file location for playlist export.
    // The location is set to the last used directory for import/export and the file
    // name to the playlist name.
    QString playlist_filename = playlistName;
    QString file_location = QFileDialog::getSaveFileName(
            NULL,
            tr("Export Playlist"),
            lastPlaylistDirectory.append("/").append(playlist_filename),
            tr("M3U Playlist (*.m3u);;M3U8 Playlist (*.m3u8);;"
            "PLS Playlist (*.pls);;Text CSV (*.csv);;Readable Text (*.txt)"));
    // Exit method if user cancelled the open dialog.
    if (file_location.isNull() || file_location.isEmpty()) {
        return;
    }

    // tro's lambda idea. This code calls asynchronously!
    m_pTrackCollection->callAsync(
            [this, file_location](TrackCollectionPrivate* pTrackCollectionPrivate) {
        // Update the import/export playlist directory
        QString fileLocation(file_location);
        QFileInfo fileName(file_location); 
        m_pConfig->set(ConfigKey("[Library]","LastImportExportPlaylistDirectory"),
                       ConfigValue(fileName.dir().absolutePath()));

        // The user has picked a new directory via a file dialog. This means the
        // system sandboxer (if we are sandboxed) has granted us permission to this
        // folder. We don't need access to this file on a regular basis so we do not
        // register a security bookmark.

        // Create a new table model since the main one might have an active search.
        // This will only export songs that we think exist on default
        QScopedPointer<PlaylistTableModel> pPlaylistTableModel(
                new PlaylistTableModel(this, m_pTrackCollection,
                        "mixxx.db.model.playlist_export"));

        pPlaylistTableModel->setTableModel(m_pPlaylistTableModel->getPlaylist());
        pPlaylistTableModel->setSort(pPlaylistTableModel->fieldIndex(
                ColumnCache::COLUMN_PLAYLISTTRACKSTABLE_POSITION), Qt::AscendingOrder);
        pPlaylistTableModel->select(pTrackCollectionPrivate);

        // check config if relative paths are desired
        bool useRelativePath = static_cast<bool>(m_pConfig->getValueString(
                ConfigKey("[Library]", "UseRelativePathOnExport")).toInt());

        if (fileLocation.endsWith(".csv", Qt::CaseInsensitive)) {
            ParserCsv::writeCSVFile(fileLocation, pPlaylistTableModel.data(), useRelativePath);
        } else if (fileLocation.endsWith(".txt", Qt::CaseInsensitive)) {
            if (pTrackCollectionPrivate->getPlaylistDAO().getHiddenType(
                    pPlaylistTableModel->getPlaylist()) == PlaylistDAO::PLHT_SET_LOG) {
                ParserCsv::writeReadableTextFile(fileLocation, pPlaylistTableModel.data(), true);
            } else {
                ParserCsv::writeReadableTextFile(fileLocation, pPlaylistTableModel.data(), false);
            }
        } else {
            // Create and populate a list of files of the playlist
            QList<QString> playlist_items;
            int rows = pPlaylistTableModel->rowCount();
            for (int i = 0; i < rows; ++i) {
                QModelIndex index = pPlaylistTableModel->index(i, 0);
                playlist_items << pPlaylistTableModel->getTrackLocation(index);
            }

            if (fileLocation.endsWith(".pls", Qt::CaseInsensitive)) {
                ParserPls::writePLSFile(fileLocation, playlist_items, useRelativePath);
            } else if (fileLocation.endsWith(".m3u8", Qt::CaseInsensitive)) {
                ParserM3u::writeM3U8File(fileLocation, playlist_items, useRelativePath);
            } else {
                //default export to M3U if file extension is missing
                if(!fileLocation.endsWith(".m3u", Qt::CaseInsensitive))
                {
                    qDebug() << "Crate export: No valid file extension specified. Appending .m3u "
                             << "and exporting to M3U.";
                    fileLocation.append(".m3u");
                }
                ParserM3u::writeM3UFile(fileLocation, playlist_items, useRelativePath);
            }
        }
    }, __PRETTY_FUNCTION__);
}

void BasePlaylistFeature::slotAddToAutoDJ() {
    qDebug() << "slotAddToAutoDJ() row:" << m_lastRightClickedIndex.data();
    addToAutoDJ(false); // Top = True
}

void BasePlaylistFeature::slotAddToAutoDJTop() {
    qDebug() << "slotAddToAutoDJTop() row:" << m_lastRightClickedIndex.data();
    addToAutoDJ(true); // bTop = True
}

void BasePlaylistFeature::addToAutoDJ(bool bTop) {
    //qDebug() << "slotAddToAutoDJ() row:" << m_lastRightClickedIndex.data();
    if (m_lastRightClickedIndex.isValid()) {
        int playlistId = playlistIdFromIndex(m_lastRightClickedIndex);
        if (playlistId >= 0) {
            // tro's lambda idea. This code calls asynchronously!
            m_pTrackCollection->callAsync(
                        [this, playlistId, bTop] (
                                TrackCollectionPrivate* pTrackCollectionPrivate) {
                // Insert this playlist
                pTrackCollectionPrivate->getPlaylistDAO()
                        .addPlaylistToAutoDJQueue(playlistId, bTop);
            }, __PRETTY_FUNCTION__);
        }
    }
}

// Must be called from Main thread
void BasePlaylistFeature::slotAnalyzePlaylist() {
    if (m_lastRightClickedIndex.isValid()) {
        int playlistId = playlistIdFromIndex(m_lastRightClickedIndex);
        if (playlistId >= 0) {
            // tro's lambda idea. This code calls Asynchronously!
            m_pTrackCollection->callAsync(
                        [this, playlistId] (TrackCollectionPrivate* pTrackCollectionPrivate) {
                QList<int> ids;
                ids = pTrackCollectionPrivate->getPlaylistDAO().getTrackIds(playlistId);
                emit(analyzeTracks(ids));
            }, __PRETTY_FUNCTION__);
        }
    }
}

TreeItemModel* BasePlaylistFeature::getChildModel() {
    return &m_childModel;
}

void BasePlaylistFeature::bindWidget(WLibrary* libraryWidget,
                                     MixxxKeyboard* keyboard) {
    Q_UNUSED(keyboard);
    WLibraryTextBrowser* edit = new WLibraryTextBrowser(libraryWidget);
    edit->setHtml(getRootViewHtml());
    edit->setOpenLinks(false);
    connect(edit, SIGNAL(anchorClicked(const QUrl)),
            this, SLOT(htmlLinkClicked(const QUrl)));
    libraryWidget->registerView(m_rootViewName, edit);
}

void BasePlaylistFeature::htmlLinkClicked(const QUrl & link) {
    if (QString(link.path())=="create") {
        slotCreatePlaylist();
    } else {
        qDebug() << "Unknonw playlist link clicked" << link.path();
    }
}

/**
  * Purpose: When inserting or removing playlists,
  * we require the sidebar model not to reset.
  * This method queries the database and does dynamic insertion
*/
QModelIndex BasePlaylistFeature::constructChildModel(int selected_id) {
    buildPlaylistList();
    QList<TreeItem*> data_list;
    int selected_row = -1;
    // Access the invisible root item
    TreeItem* root = m_childModel.getItem(QModelIndex());

    int row = 0;
    for (QList<QPair<int, QString> >::const_iterator it = m_playlistList.begin();
         it != m_playlistList.end(); ++it, ++row) {
        int playlist_id = it->first;
        QString playlist_name = it->second;

        if (selected_id == playlist_id) {
            // save index for selection
            selected_row = row;
            m_childModel.index(selected_row, 0);
        }

        // Create the TreeItem whose parent is the invisible root item
        TreeItem* item = new TreeItem(playlist_name, QString::number(playlist_id), this, root);
        decorateChild(item, playlist_id);
        data_list.append(item);
    }

    // Append all the newly created TreeItems in a dynamic way to the childmodel
    m_childModel.insertRows(data_list, 0, m_playlistList.size());
    if (selected_row == -1) {
        return QModelIndex();
    }
    return m_childModel.index(selected_row, 0);
}

/**
  * Clears the child model dynamically, but the invisible root item remains
  */
void BasePlaylistFeature::clearChildModel() {
    m_childModel.removeRows(0, m_playlistList.size());
}
