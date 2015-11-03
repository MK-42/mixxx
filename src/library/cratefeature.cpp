// cratefeature.cpp
// Created 10/22/2009 by RJ Ryan (rryan@mit.edu)

#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QDesktopServices>

#include "library/cratefeature.h"
#include "library/parser.h"
#include "library/parserm3u.h"
#include "library/parserpls.h"
#include "library/parsercsv.h"
#include "library/queryutil.h"

#include "library/cratetablemodel.h"
#include "library/trackcollection.h"
#include "widget/wlibrarytextbrowser.h"
#include "widget/wlibrary.h"
#include "mixxxkeyboard.h"
#include "treeitem.h"
#include "soundsourceproxy.h"
#include "util/dnd.h"

CrateFeature::CrateFeature(QObject* parent,
                           TrackCollection* pTrackCollection,
                           ConfigObject<ConfigValue>* pConfig)
        : m_pTrackCollection(pTrackCollection),
          m_crateTableModel(this, pTrackCollection),
          m_pConfig(pConfig) {
    Q_UNUSED(parent);
    m_pCreateCrateAction = new QAction(tr("Create New Crate"),this);
    connect(m_pCreateCrateAction, SIGNAL(triggered()),
            this, SLOT(slotCreateCrate()));

    m_pDeleteCrateAction = new QAction(tr("Remove"),this);
    connect(m_pDeleteCrateAction, SIGNAL(triggered()),
            this, SLOT(slotDeleteCrate()));

    m_pRenameCrateAction = new QAction(tr("Rename"),this);
    connect(m_pRenameCrateAction, SIGNAL(triggered()),
            this, SLOT(slotRenameCrate()));

    m_pLockCrateAction = new QAction(tr("Lock"),this);
    connect(m_pLockCrateAction, SIGNAL(triggered()),
            this, SLOT(slotToggleCrateLock()));

    m_pImportPlaylistAction = new QAction(tr("Import Crate"),this);
    connect(m_pImportPlaylistAction, SIGNAL(triggered()),
            this, SLOT(slotImportPlaylist()));

    m_pExportPlaylistAction = new QAction(tr("Export Crate"), this);
    connect(m_pExportPlaylistAction, SIGNAL(triggered()),
            this, SLOT(slotExportPlaylist()));

    m_pDuplicateCrateAction = new QAction(tr("Duplicate"),this);
    connect(m_pDuplicateCrateAction, SIGNAL(triggered()),
            this, SLOT(slotDuplicateCrate()));

    m_pAnalyzeCrateAction = new QAction(tr("Analyze entire Crate"),this);
    connect(m_pAnalyzeCrateAction, SIGNAL(triggered()),
            this, SLOT(slotAnalyzeCrate()));

#ifdef __AUTODJCRATES__

    m_pAutoDjTrackSource = new QAction(tr("Auto DJ Track Source"),this);
    m_pAutoDjTrackSource->setCheckable(true);
    connect(m_pAutoDjTrackSource, SIGNAL(changed()),
            this, SLOT(slotAutoDjTrackSourceChanged()));

#endif // __AUTODJCRATES__

    m_pTrackCollection->callSync( [this] (TrackCollectionPrivate* pTrackCollectionPrivate) {
        connect(&pTrackCollectionPrivate->getCrateDAO(), SIGNAL(added(int)),
                this, SLOT(slotCrateTableChanged(int)), Qt::QueuedConnection);

        connect(&pTrackCollectionPrivate->getCrateDAO(), SIGNAL(deleted(int)),
                this, SLOT(slotCrateTableChanged(int)), Qt::QueuedConnection);

        connect(&pTrackCollectionPrivate->getCrateDAO(), SIGNAL(renamed(int,QString)),
            this, SLOT(slotCrateTableRenamed(int,QString)), Qt::QueuedConnection);

        connect(&pTrackCollectionPrivate->getCrateDAO(), SIGNAL(lockChanged(int)),
                this, SLOT(slotCrateTableChanged(int)), Qt::QueuedConnection);
    }, __PRETTY_FUNCTION__);

    // construct child model
    TreeItem *rootItem = new TreeItem();
    m_childModel.setRootItem(rootItem);
    constructChildModel(-1);
}

CrateFeature::~CrateFeature() {
    //delete QActions
    delete m_pCreateCrateAction;
    delete m_pDeleteCrateAction;
    delete m_pRenameCrateAction;
    delete m_pDuplicateCrateAction;
    delete m_pLockCrateAction;
    delete m_pImportPlaylistAction;
    delete m_pAnalyzeCrateAction;
#ifdef __AUTODJCRATES__
    delete m_pAutoDjTrackSource;
#endif // __AUTODJCRATES__
}

QVariant CrateFeature::title() {
    return tr("Crates");
}

QIcon CrateFeature::getIcon() {
    return QIcon(":/images/library/ic_library_crates.png");
}

// Must be called from Main thread
bool CrateFeature::dropAcceptChild(const QModelIndex& index, QList<QUrl> urls,
                                   QObject* pSource) {
    QString crateName = index.data().toString();
    QList<QFileInfo> files = DragAndDropHelper::supportedTracksFromUrls(urls, false, true);

    bool result = false;
    const bool is_pSource = pSource;
    // tro's lambda idea. This code calls synchronously!
    m_pTrackCollection->callSync(
            [this, &crateName, &files, &is_pSource, &result] (TrackCollectionPrivate* pTrackCollectionPrivate) {
        int crateId = pTrackCollectionPrivate->getCrateDAO().getCrateIdByName(crateName);
        QList<int> trackIds;
        if (is_pSource) {
            trackIds = pTrackCollectionPrivate->getTrackDAO().getTrackIds(files);
        } else {
            // Adds track, does not insert duplicates, handles unremoving logic.
            trackIds = pTrackCollectionPrivate->getTrackDAO().addTracks(files, true);
        }
        qDebug() << "CrateFeature::dropAcceptChild adding tracks"
                 << trackIds.size() << " to crate "<< crateId;
        // remove tracks that could not be added
        for (int trackId = 0; trackId < trackIds.size(); trackId++) {
            if (trackIds.at(trackId) < 0) {
                trackIds.removeAt(trackId--);
            }
        }
        int tracksAdded = pTrackCollectionPrivate->getCrateDAO().addTracksToCrate(crateId, &trackIds);
        result = tracksAdded > 0;
    }, __PRETTY_FUNCTION__);
    return result;
}

// Must be called from TrackCollection
bool CrateFeature::dragMoveAcceptChild(const QModelIndex& index, QUrl url,
                                       TrackCollectionPrivate* pTrackCollectionPrivate) {
    //TODO: Filter by supported formats regex and reject anything that doesn't match.
    QString crateName = index.data().toString();

    int crateId = pTrackCollectionPrivate->getCrateDAO().getCrateIdByName(crateName);
    bool locked = pTrackCollectionPrivate->getCrateDAO().isCrateLocked(crateId);
    QFileInfo file(url.toLocalFile());
    bool formatSupported = SoundSourceProxy::isFilenameSupported(file.fileName());
    return !locked && formatSupported;
}

void CrateFeature::bindWidget(WLibrary* libraryWidget,
                              MixxxKeyboard* keyboard) {
    Q_UNUSED(keyboard);
    WLibraryTextBrowser* edit = new WLibraryTextBrowser(libraryWidget);
    edit->setHtml(getRootViewHtml());
    edit->setOpenLinks(false);
    connect(edit, SIGNAL(anchorClicked(const QUrl)),
        this, SLOT(htmlLinkClicked(const QUrl))
    );
    libraryWidget->registerView("CRATEHOME", edit);
}

TreeItemModel* CrateFeature::getChildModel() {
    return &m_childModel;
}

void CrateFeature::activate() {
    emit(switchToView("CRATEHOME"));
    emit(restoreSearch(QString())); //disable search on crate home
}

// Must be called from Main thread
void CrateFeature::activateChild(const QModelIndex& index) {
    if (!index.isValid())
        return;
    const QString crateName = index.data().toString();
    int crateId;
    // tro's lambda idea. This code calls Synchronously!
    m_pTrackCollection->callSync(
            [this, &crateName, &crateId] (TrackCollectionPrivate* pTrackCollectionPrivate) {
        crateId = pTrackCollectionPrivate->getCrateDAO().getCrateIdByName(crateName);
    }, __PRETTY_FUNCTION__);

    m_crateTableModel.setTableModel(crateId);
    emit(showTrackModel(&m_crateTableModel));
}

void CrateFeature::onRightClick(const QPoint& globalPos) {
    m_lastRightClickedIndex = QModelIndex();
    QMenu menu(NULL);
    menu.addAction(m_pCreateCrateAction);
    menu.exec(globalPos);
}

void CrateFeature::onRightClickChild(const QPoint& globalPos, QModelIndex index) {
    //Save the model index so we can get it in the action slots...
    m_lastRightClickedIndex = index;
    QString crateName = index.data().toString();
    int crateId = -1;
    bool locked = false;
    // tro's lambda idea. This code calls synchronously!
    m_pTrackCollection->callSync(
            [this, &crateId, &locked, &crateName] (TrackCollectionPrivate* pTrackCollectionPrivate) {
        crateId = pTrackCollectionPrivate->getCrateDAO().getCrateIdByName(crateName);
        locked = pTrackCollectionPrivate->getCrateDAO().isCrateLocked(crateId);
    }, __PRETTY_FUNCTION__);

    m_pDeleteCrateAction->setEnabled(!locked);
    m_pRenameCrateAction->setEnabled(!locked);

#ifdef __AUTODJCRATES__
    bool bAutoDj;
    // tro's lambda idea. This code calls synchronously!
    m_pTrackCollection->callSync(
            [this, &bAutoDj, &crateId] (TrackCollectionPrivate* pTrackCollectionPrivate) {
        bAutoDj = pTrackCollectionPrivate->getCrateDAO().isCrateInAutoDj(crateId);
    }, __PRETTY_FUNCTION__);
    m_pAutoDjTrackSource->setChecked(bAutoDj);
#endif // __AUTODJCRATES__

    m_pLockCrateAction->setText(locked ? tr("Unlock") : tr("Lock"));

    QMenu menu(NULL);
    menu.addAction(m_pCreateCrateAction);
    menu.addSeparator();
    menu.addAction(m_pRenameCrateAction);
    menu.addAction(m_pDuplicateCrateAction);
    menu.addAction(m_pDeleteCrateAction);
    menu.addAction(m_pLockCrateAction);
    menu.addSeparator();
#ifdef __AUTODJCRATES__
    menu.addAction(m_pAutoDjTrackSource);
    menu.addSeparator();
#endif // __AUTODJCRATES__
    menu.addAction(m_pAnalyzeCrateAction);
    menu.addSeparator();
    menu.addAction(m_pImportPlaylistAction);
    menu.addAction(m_pExportPlaylistAction);
    menu.exec(globalPos);
}

void CrateFeature::slotCreateCrate() {
    QString name;
    bool validNameGiven = false;

    while (!validNameGiven) {
        bool ok = false;
        name = QInputDialog::getText(NULL,
                                     tr("Create New Crate"),
                                     tr("Enter name for new crate:"),
                                     QLineEdit::Normal, tr("New Crate"),
                                     &ok).trimmed();

        if (!ok)
            return;

        int existingId;
        // tro's lambda idea. This code calls synchronously!
        m_pTrackCollection->callSync(
                [this, &existingId, &name] (TrackCollectionPrivate* pTrackCollectionPrivate) {
            existingId = pTrackCollectionPrivate->getCrateDAO().getCrateIdByName(name);
        }, __PRETTY_FUNCTION__);

        if (existingId != -1) {
            QMessageBox::warning(NULL,
                                 tr("Creating Crate Failed"),
                                 tr("A crate by that name already exists."));
        } else if (name.isEmpty()) {
            QMessageBox::warning(NULL,
                                 tr("Creating Crate Failed"),
                                 tr("A crate cannot have a blank name."));
        } else {
            validNameGiven = true;
        }
    }

    int crateId;
    // tro's lambda idea. This code calls synchronously!
    m_pTrackCollection->callSync(
            [this, &crateId, &name] (TrackCollectionPrivate* pTrackCollectionPrivate) {
        crateId = pTrackCollectionPrivate->getCrateDAO().createCrate(name);
    }, __PRETTY_FUNCTION__);

    if (crateId != -1) {
        emit(showTrackModel(&m_crateTableModel));
    } else {
        qDebug() << "Error creating crate with name " << name;
        QMessageBox::warning(NULL,
                             tr("Creating Crate Failed"),
                             tr("An unknown error occurred while creating crate: ")
                             + name);
    }
}

void CrateFeature::slotDeleteCrate() {
    QString crateName = m_lastRightClickedIndex.data().toString();
    int crateId = 0;
    bool locked = false;
    bool deleted = false;
    // tro's lambda idea. This code calls synchronously!
    m_pTrackCollection->callSync(
                [this, &crateId, &locked, &crateName, &deleted] (TrackCollectionPrivate* pTrackCollectionPrivate) {
        crateId = pTrackCollectionPrivate->getCrateDAO().getCrateIdByName(crateName);
        locked = pTrackCollectionPrivate->getCrateDAO().isCrateLocked(crateId);

        if (locked) {
            qDebug() << "Skipping crate deletion because crate" << crateId << "is locked.";
            return;
        }

        deleted = pTrackCollectionPrivate->getCrateDAO().deleteCrate(crateId);
    }, __PRETTY_FUNCTION__);

    if (deleted) {
        activate();
    } else {
        qDebug() << "Failed to delete crateId" << crateId;
    }
}

void CrateFeature::slotRenameCrate() {
    QString oldName = m_lastRightClickedIndex.data().toString();
    int crateId = -1;
    bool locked = false;
    // tro's lambda idea. This code calls synchronously!
    m_pTrackCollection->callSync(
                [this, &crateId, &locked, &oldName] (TrackCollectionPrivate* pTrackCollectionPrivate) {
        crateId = pTrackCollectionPrivate->getCrateDAO().getCrateIdByName(oldName);
        locked = pTrackCollectionPrivate->getCrateDAO().isCrateLocked(crateId);
    }, __PRETTY_FUNCTION__);

    if (locked) {
        qDebug() << "Skipping crate rename because crate" << crateId << "is locked.";
        return;
    }

    QString newName;
    bool validNameGiven = false;

    while (!validNameGiven) {
        bool ok = false;
        newName = QInputDialog::getText(NULL,
                                        tr("Rename Crate"),
                                        tr("Enter new name for crate:"),
                                        QLineEdit::Normal,
                                        oldName,
                                        &ok).trimmed();

        if (!ok || newName == oldName) {
            return;
        }

        int existingId = -1;
        // tro's lambda idea. This code calls synchronously!
        m_pTrackCollection->callSync(
                    [this, &existingId, &newName] (TrackCollectionPrivate* pTrackCollectionPrivate) {
            existingId = pTrackCollectionPrivate->getCrateDAO().getCrateIdByName(newName);
        }, __PRETTY_FUNCTION__);

        if (existingId != -1) {
            QMessageBox::warning(NULL,
                                tr("Renaming Crate Failed"),
                                tr("A crate by that name already exists."));
        } else if (newName.isEmpty()) {
            QMessageBox::warning(NULL,
                                tr("Renaming Crate Failed"),
                                tr("A crate cannot have a blank name."));
        } else {
            validNameGiven = true;
        }
    }

    bool renameResult = false;
    // tro's lambda idea. This code calls synchronously!
    m_pTrackCollection->callSync(
                [this, &renameResult, &crateId, &newName] (TrackCollectionPrivate* pTrackCollectionPrivate) {
        renameResult = pTrackCollectionPrivate->getCrateDAO().renameCrate(crateId, newName);
    }, __PRETTY_FUNCTION__);

    if (!renameResult) {
        qDebug() << "Failed to rename crateId" << crateId;
    }
}

void CrateFeature::slotDuplicateCrate() {
    QString oldName = m_lastRightClickedIndex.data().toString();
    int oldCrateId = -1;
    // tro's lambda idea. This code calls synchronously!
    m_pTrackCollection->callSync(
                [this, &oldCrateId, &oldName] (TrackCollectionPrivate* pTrackCollectionPrivate) {
        oldCrateId = pTrackCollectionPrivate->getCrateDAO().getCrateIdByName(oldName);
    }, __PRETTY_FUNCTION__);

    QString name;
    bool validNameGiven = false;

    while (!validNameGiven) {
        bool ok = false;
        name = QInputDialog::getText(NULL,
                                        tr("Duplicate Crate"),
                                        tr("Enter name for new crate:"),
                                        QLineEdit::Normal,
                                        //: Appendix to default name when duplicating a crate
                                        oldName + tr("_copy" , "[noun]"),
                                        &ok).trimmed();

        if (!ok || name == oldName) {
            return;
        }

        int existingId = -1;
        // tro's lambda idea. This code calls synchronously!
        m_pTrackCollection->callSync(
                    [this, &existingId, &name] (TrackCollectionPrivate* pTrackCollectionPrivate) {
            existingId = pTrackCollectionPrivate->getCrateDAO().getCrateIdByName(name);
        }, __PRETTY_FUNCTION__);

        if (existingId != -1) {
            QMessageBox::warning(NULL,
                                tr("Renaming Crate Failed"),
                                tr("A crate by that name already exists."));
        } else if (name.isEmpty()) {
            QMessageBox::warning(NULL,
                                tr("Renaming Crate Failed"),
                                tr("A crate cannot have a blank name."));
        } else {
            validNameGiven = true;
        }
    }

    int newCrateId = -1;
    // tro's lambda idea. This code calls synchronously!
    m_pTrackCollection->callSync(
                [this, &newCrateId, &oldCrateId, &name] (TrackCollectionPrivate* pTrackCollectionPrivate) {
        newCrateId = pTrackCollectionPrivate->getCrateDAO().createCrate(name);
        pTrackCollectionPrivate->getCrateDAO().copyCrateTracks(oldCrateId, newCrateId);
    }, __PRETTY_FUNCTION__);

    if (newCrateId != -1) {
        emit(showTrackModel(&m_crateTableModel));
    } else {
        qDebug() << "Error creating crate with name " << name;
        QMessageBox::warning(NULL,
                             tr("Creating Crate Failed"),
                             tr("An unknown error occurred while creating crate: ")
                             + name);
    }
}

void CrateFeature::slotToggleCrateLock() {
    QString crateName = m_lastRightClickedIndex.data().toString();
    // tro's lambda idea. This code calls asynchronously!
    m_pTrackCollection->callAsync(
                [this, crateName] (TrackCollectionPrivate* pTrackCollectionPrivate) {
        int crateId = pTrackCollectionPrivate->getCrateDAO().getCrateIdByName(crateName);
        bool locked = !pTrackCollectionPrivate->getCrateDAO().isCrateLocked(crateId);

        if (!pTrackCollectionPrivate->getCrateDAO().setCrateLocked(crateId, locked)) {
            qDebug() << "Failed to toggle lock of crateId " << crateId;
        }
    }, __PRETTY_FUNCTION__);
}

void CrateFeature::slotAutoDjTrackSourceChanged() {
#ifdef __AUTODJCRATES__
    QString crateName = m_lastRightClickedIndex.data().toString();
    // tro's lambda idea. This code calls asynchronously!
    m_pTrackCollection->callAsync(
                [this, crateName] (TrackCollectionPrivate* pTrackCollectionPrivate) {
        int crateId = pTrackCollectionPrivate->getCrateDAO().getCrateIdByName(crateName);
        if (crateId != -1) {
            pTrackCollectionPrivate->getCrateDAO().setCrateInAutoDj(crateId, m_pAutoDjTrackSource->isChecked());
        }
    });
#endif // __AUTODJCRATES__
}

// Must be called from Main thread
void CrateFeature::buildCrateList() {
    m_crateList.clear();

    // tro's lambda idea. This code calls synchronously!
    m_pTrackCollection->callSync(
                [this] (TrackCollectionPrivate* pTrackCollectionPrivate) {
        QSqlTableModel crateListTableModel(this, pTrackCollectionPrivate->getDatabase());
        crateListTableModel.setTable("crates");
        crateListTableModel.setSort(crateListTableModel.fieldIndex("name"),
                                    Qt::AscendingOrder);
        crateListTableModel.setFilter("show = 1");
        crateListTableModel.select();
        while (crateListTableModel.canFetchMore()) {
            crateListTableModel.fetchMore();
        }
        QSqlRecord record = crateListTableModel.record();
        int nameColumn = record.indexOf("name");
        int idColumn = record.indexOf("id");

        for (int row = 0; row < crateListTableModel.rowCount(); ++row) {
            int id = crateListTableModel.data(
                        crateListTableModel.index(row, idColumn)).toInt();
            QString name = crateListTableModel.data(
                        crateListTableModel.index(row, nameColumn)).toString();
            m_crateList.append(qMakePair(id, name));
        }
    }, __PRETTY_FUNCTION__);
}

/**
  * Purpose: When inserting or removing playlists,
  * we require the sidebar model not to reset.
  * This method queries the database and does dynamic insertion
*/
// Must be called from Main thread
QModelIndex CrateFeature::constructChildModel(int selected_id) {
    buildCrateList();
    QList<TreeItem*> data_list;
    int selected_row = -1;
    // Access the invisible root item
    TreeItem* root = m_childModel.getItem(QModelIndex());

    int row = 0;
    for (QList<QPair<int, QString> >::const_iterator it = m_crateList.begin();
         it != m_crateList.end(); ++it, ++row) {
        int crate_id = it->first;
        QString crate_name = it->second;

        if (selected_id == crate_id) {
            // save index for selection
            selected_row = row;
            m_childModel.index(selected_row, 0);
        }

        // Create the TreeItem whose parent is the invisible root item
        TreeItem* item = new TreeItem(crate_name, crate_name, this, root);

        bool locked = false;
        // tro's lambda idea. This code calls synchronously!
        m_pTrackCollection->callSync(
                    [this, &locked, crate_id] (TrackCollectionPrivate* pTrackCollectionPrivate) {
            locked = pTrackCollectionPrivate->getCrateDAO().isCrateLocked(crate_id);
        }, __PRETTY_FUNCTION__);

        item->setIcon(locked ? QIcon(":/images/library/ic_library_locked.png") : QIcon());
        data_list.append(item);
    }

    // Append all the newly created TreeItems in a dynamic way to the childmodel
    m_childModel.insertRows(data_list, 0, m_crateList.size());
    if (selected_row == -1) {
        return QModelIndex();
    }
    return m_childModel.index(selected_row, 0);
}

/**
  * Clears the child model dynamically
  */
void CrateFeature::clearChildModel() {
    m_childModel.removeRows(0, m_crateList.size());
    m_crateList.clear();
}

void CrateFeature::slotImportPlaylist() {
    qDebug() << "slotImportPlaylist() row:" ; //<< m_lastRightClickedIndex.data();

    QString lastCrateDirectory = m_pConfig->getValueString(
            ConfigKey("[Library]", "LastImportExportCrateDirectory"),
            QDesktopServices::storageLocation(QDesktopServices::MusicLocation));

    QString playlist_file = QFileDialog::getOpenFileName(
        NULL,
        tr("Import Playlist"),
        lastCrateDirectory,
        tr("Playlist Files (*.m3u *.m3u8 *.pls *.csv)"));
    // Exit method if user cancelled the open dialog.
    if (playlist_file.isNull() || playlist_file.isEmpty() ) return;

    // Update the import/export crate directory
    QFileInfo fileName(playlist_file);
    m_pConfig->set(ConfigKey("[Library]","LastImportExportCrateDirectory"),
                   ConfigValue(fileName.dir().absolutePath()));

    // The user has picked a new directory via a file dialog. This means the
    // system sandboxer (if we are sandboxed) has granted us permission to this
    // folder. We don't need access to this file on a regular basis so we do not
    // register a security bookmark.

    Parser* playlist_parser = NULL;

    if (playlist_file.endsWith(".m3u", Qt::CaseInsensitive) ||
        playlist_file.endsWith(".m3u8", Qt::CaseInsensitive)) {
        // .m3u8 is Utf8 representation of an m3u playlist
        playlist_parser = new ParserM3u();
    } else if (playlist_file.endsWith(".pls", Qt::CaseInsensitive)) {
        playlist_parser = new ParserPls();
    } else if (playlist_file.endsWith(".csv", Qt::CaseInsensitive)) {
        playlist_parser = new ParserCsv();
    } else {
        return;
    }

    QList<QString> entries = playlist_parser->parse(playlist_file);
    //qDebug() << "Size of Imported Playlist: " << entries.size();

    //Iterate over the List that holds URLs of playlist entires
    m_crateTableModel.addTracks(QModelIndex(), entries);

    //delete the parser object
    if (playlist_parser)
        delete playlist_parser;
}

// Must be called from Main thread
void CrateFeature::slotAnalyzeCrate() {
    if (m_lastRightClickedIndex.isValid()) {
        const QString name = m_lastRightClickedIndex.data().toString();

        // tro's lambda idea. This code calls asynchronously!
        m_pTrackCollection->callAsync(
                    [this, name] (TrackCollectionPrivate* pTrackCollectionPrivate) {
            int playlistId = pTrackCollectionPrivate->getCrateDAO().getCrateIdByName(name);
            if (playlistId >= 0) {
                QList<int> ids = pTrackCollectionPrivate->getCrateDAO().getTrackIds(playlistId);
                emit(analyzeTracks(ids));
            }
        }, __PRETTY_FUNCTION__);
    }
}

// Must be called from Main thread
void CrateFeature::slotExportPlaylist() {
    qDebug() << "Export crate" << m_lastRightClickedIndex.data();

    QString lastCrateDirectory = m_pConfig->getValueString(
            ConfigKey("[Library]", "LastImportExportCrateDirectory"),
            QDesktopServices::storageLocation(QDesktopServices::MusicLocation));

    QString file_location = QFileDialog::getSaveFileName(
        NULL,
        tr("Export Crate"),
        lastCrateDirectory,
        tr("M3U Playlist (*.m3u);;M3U8 Playlist (*.m3u8);;PLS Playlist (*.pls);;Text CSV (*.csv);;Readable Text (*.txt)"));
    // Exit method if user cancelled the open dialog.
    if (file_location.isNull() || file_location.isEmpty()) {
        return;
    }

    // Update the import/export crate directory
    QFileInfo fileName(file_location);
    m_pConfig->set(ConfigKey("[Library]","LastImportExportCrateDirectory"),
                ConfigValue(fileName.dir().absolutePath()));

    // The user has picked a new directory via a file dialog. This means the
    // system sandboxer (if we are sandboxed) has granted us permission to this
    // folder. We don't need access to this file on a regular basis so we do not
    // register a security bookmark.

    // check config if relative paths are desired
    bool useRelativePath = static_cast<bool>(
        m_pConfig->getValueString(
            ConfigKey("[Library]", "UseRelativePathOnExport")).toInt());

    // Create list of files of the crate
    QList<QString> playlist_items;
    // Create a new table model since the main one might have an active search.
    QScopedPointer<CrateTableModel> pCrateTableModel(
        new CrateTableModel(this, m_pTrackCollection));

        pCrateTableModel->setTableModel(m_crateTableModel.getCrate());

        // tro's lambda idea. This code calls synchronously!
        m_pTrackCollection->callSync(
                    [this, &pCrateTableModel] (TrackCollectionPrivate* pTrackCollectionPrivate) {
            pCrateTableModel->select(pTrackCollectionPrivate);
        }, __PRETTY_FUNCTION__);

    if (file_location.endsWith(".csv", Qt::CaseInsensitive)) {
            ParserCsv::writeCSVFile(file_location, pCrateTableModel.data(), useRelativePath);
    } else if (file_location.endsWith(".txt", Qt::CaseInsensitive)) {
             ParserCsv::writeReadableTextFile(file_location, pCrateTableModel.data(), false);
    } else{
        // populate a list of files of the crate
        QList<QString> playlist_items;
        int rows = pCrateTableModel->rowCount();
        for (int i = 0; i < rows; ++i) {
            QModelIndex index = m_crateTableModel.index(i, 0);
            playlist_items << m_crateTableModel.getTrackLocation(index);
        }

        if (file_location.endsWith(".pls", Qt::CaseInsensitive)) {
            ParserPls::writePLSFile(file_location, playlist_items, useRelativePath);
        } else if (file_location.endsWith(".m3u8", Qt::CaseInsensitive)) {
            ParserM3u::writeM3U8File(file_location, playlist_items, useRelativePath);
        } else {
            //default export to M3U if file extension is missing
            if(!file_location.endsWith(".m3u", Qt::CaseInsensitive))
            {
                qDebug() << "Crate export: No valid file extension specified. Appending .m3u "
                         << "and exporting to M3U.";
                file_location.append(".m3u");
            }
            ParserM3u::writeM3UFile(file_location, playlist_items, useRelativePath);
        }
    }
}

// Must be called from Main thread
void CrateFeature::slotCrateTableChanged(int crateId) {
    //qDebug() << "slotPlaylistTableChanged() playlistId:" << playlistId;
    clearChildModel();

    m_lastRightClickedIndex = constructChildModel(crateId);
    // Switch the view to the crate.
    m_crateTableModel.setTableModel(crateId);
    // Update selection
    emit(featureSelect(this, m_lastRightClickedIndex));
}

void CrateFeature::slotCrateTableRenamed(int a_iCrateId,
                                         QString /* a_strName */) {
    slotCrateTableChanged(a_iCrateId);
}

void CrateFeature::htmlLinkClicked(const QUrl & link) {
    if (QString(link.path())=="create") {
        slotCreateCrate();
    } else {
        qDebug() << "Unknown crate link clicked" << link;
    }
}

QString CrateFeature::getRootViewHtml() const {
    QString cratesTitle = tr("Crates");
    QString cratesSummary = tr("Crates are a great way to help organize the music you want to DJ with.");
    QString cratesSummary2 = tr("Make a crate for your next gig, for your favorite electrohouse tracks, or for your most requested songs.");
    QString cratesSummary3 = tr("Crates let you organize your music however you'd like!");

    QString html;
    QString createCrateLink = tr("Create New Crate");
    html.append(QString("<h2>%1</h2>").arg(cratesTitle));
    html.append("<table border=\"0\" cellpadding=\"5\"><tr><td>");
    html.append(QString("<p>%1</p>").arg(cratesSummary));
    html.append(QString("<p>%1</p>").arg(cratesSummary2));
    html.append(QString("<p>%1</p>").arg(cratesSummary3));
    html.append("</td><td rowspan=\"2\">");
    html.append("<img src=\"qrc:/images/library/crates_art.png\">");
    html.append("</td></tr>");
    html.append(
        QString("<tr><td><a href=\"create\">%1</a>").arg(createCrateLink)
    );
    html.append("</td></tr></table>");
    return html;
}
