#ifndef DLTFILEINDEXER_H
#define DLTFILEINDEXER_H

#include <QObject>
#include <QThread>
#include <QProgressDialog>
#include <QMainWindow>
#include <QPair>
#include <QMutex>

#include "qdlt.h"

#define DLT_FILE_INDEXER_SEG_SIZE (1024*1024)
#define DLT_FILE_INDEXER_FILE_VERSION 1

class DltFileIndexer : public QThread
{
    Q_OBJECT
public:

    // constructors
    explicit DltFileIndexer(QObject *parent = 0);
    DltFileIndexer(QDltFile *argFile, QDltPluginManager *pluginManager, QDltDefaultFilter *defaultFilter, QMainWindow *parent = 0);

    // destructor
    ~DltFileIndexer();

    typedef enum { modeNone, modeIndex, modeIndexAndFilter, modeFilter, modeDefaultFilter } IndexingMode;

    // create main index
    bool index();

    // create index based on filters and apply plugins
    bool indexFilter();
    bool indexDefaultFilter();

    // load/save filter index from/to file
    bool loadFilterIndexCache(QDltFilterList &filterList, QList<unsigned long> &index);
    bool saveFilterIndexCache(QDltFilterList &filterList, QList<unsigned long> &index);
    QString filenameFilterIndexCache(QDltFilterList &filterList);

    // load/save index from/to file
    bool loadIndexCache();
    bool saveIndexCache();
    QString filenameIndexCache();

    // load/save index from/to file
    bool saveIndex(QString filename, QList<unsigned long> &index);
    bool loadIndex(QString filename, QList<unsigned long> &index);

    // Accessors to mutex
    void lock();
    void unlock();
    bool tryLock();

    // set/get indexing mode
    void setMode(IndexingMode mode) { this->mode = mode; }
    IndexingMode getMode() { return mode; }

    // enable/disable plugins
    void setPluginsEnabled(bool enable) { pluginsEnabled = enable; }
    bool getPluginsEnabled() { return pluginsEnabled; }

    // enable/disable filters
    void setFiltersEnabled(bool enable) { filtersEnabled = enable; }
    bool getFiltersEnabled() { return filtersEnabled; }

    // enable/disable multithreaded
    void setMultithreaded(bool enable) { multithreaded = enable; }
    bool getMultithreaded() { return multithreaded; }

    // get and set filter cache
    void setFilterCache(QString path) { filterCache = path; }
    QString getFilterCache() { return filterCache; }

    // get index of all messages
    QList<unsigned long> getIndexAll() { return indexAllList; }
    QList<unsigned long> getIndexFilters() { return indexFilterList; }

    // main thread routine
    void run();

protected:

private:

    // the current set mode of indexing
    IndexingMode mode;

    /* Lock, to reserve exclusive indexing rights
     * to this component when running */
    QMutex indexLock;

    // File to work on
    QDltFile *dltFile;

    // Plugins to be used
    QDltPluginManager *pluginManager;

    // DefaultFilter to be used
    QDltDefaultFilter *defaultFilter;

    // stop flag
    bool stopFlag;

    // active plugins
    QList<QDltPlugin*> activeViewerPlugins;
    QList<QDltPlugin*> activeDecoderPlugins;

    // full index
    QList<unsigned long> indexAllList;

    // filtered index
    QList<unsigned long> indexFilterList;

    // some flags
    bool pluginsEnabled;
    bool filtersEnabled;
    bool multithreaded;

    // filter cache path
    QString filterCache;

    // run counter
    int maxRun, currentRun;

    // performance counter
    int msecsIndexCounter;
    int msecsFilterCounter;
    int msecsDefaultFilterCounter;

signals:

    // the maximum progress value
    void progressMax(quint64 index);

    // the current progress value
    void progress(quint64 index);

    // progress text change fro different parts
    void progressText(QString text);

    // version log message parsed
    void versionString(QString ecuId, QString version);

    // get log info message found
    void getLogInfo(int index);

    // index creation finished
    void finishIndex();

    // complete index creation finished
    void finishFilter();

    // complete index creation of default filter finished
    void finishDefaultFilter();

public slots:

    void stop();

};

#endif // DLTFILEINDEXER_H
