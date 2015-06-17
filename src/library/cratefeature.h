#ifndef CRATEFEATURE_H
#define CRATEFEATURE_H

#include <QModelIndex>
#include <QList>
#include <QPair>
#include <QAction>

#include "library/libraryfeature.h"
#include "library/cratetablemodel.h"

#include "treeitemmodel.h"
#include "configobject.h"

class TrackCollection;

class CrateFeature : public LibraryFeature {
    Q_OBJECT
  public:
    CrateFeature(QObject* parent,
                 TrackCollection* pTrackCollection,
                 ConfigObject<ConfigValue>* pConfig);
    virtual ~CrateFeature();

    QVariant title();
    QIcon getIcon();

    // Must be called from TrackCollection thread
    bool dropAcceptChild(const QModelIndex& index, QList<QUrl> urls,
                         QObject* pSource);
    bool dragMoveAcceptChild(const QModelIndex& index, QUrl url);

    void bindWidget(WLibrary* libraryWidget,
                    MixxxKeyboard* keyboard);

    TreeItemModel* getChildModel();

  signals:
    void analyzeTracks(QList<int>);

  public slots:
    void activate();
    void activateChild(const QModelIndex& index);
    void onRightClick(const QPoint& globalPos);
    void onRightClickChild(const QPoint& globalPos, QModelIndex index);

    void slotCreateCrate();
    void slotDeleteCrate();
    void slotRenameCrate();
    void slotDuplicateCrate();
    void slotAutoDjTrackSourceChanged();
    void slotToggleCrateLock();
    void slotImportPlaylist();
    void slotExportPlaylist();
    void slotAnalyzeCrate();
    void slotCrateTableChanged(int playlistId);
    void slotCrateTableRenamed(int playlistId, QString a_strName);
    void htmlLinkClicked(const QUrl & link);

  private:
    QString getRootViewHtml() const;
    QModelIndex constructChildModel(int selected_id);
    void clearChildModel();
    void buildCrateList();

    TrackCollection* m_pTrackCollection;
    QAction *m_pCreateCrateAction;
    QAction *m_pDeleteCrateAction;
    QAction *m_pRenameCrateAction;
    QAction *m_pLockCrateAction;
    QAction *m_pDuplicateCrateAction;
#ifdef __AUTODJCRATES__
    QAction *m_pAutoDjTrackSource;
#endif // __AUTODJCRATES__
    QAction *m_pImportPlaylistAction;
    QAction *m_pExportPlaylistAction;
    QAction *m_pAnalyzeCrateAction;
    QList<QPair<int, QString> > m_crateList;
    CrateTableModel m_crateTableModel;
    QModelIndex m_lastRightClickedIndex;
    TreeItemModel m_childModel;
    ConfigObject<ConfigValue>* m_pConfig;
};

#endif /* CRATEFEATURE_H */
