#include "dltfileindexer.h"
#include "optmanager.h"

#include <QDebug>
#include <QMessageBox>
#include <QApplication>
#include <QTime>
#include <QCryptographicHash>

extern "C" {
    #include "dlt_common.h"
    #include "dlt_user.h"
}

DltFileIndexerKey::DltFileIndexerKey(time_t time,unsigned int microseconds)
{
    this->time = time;
    this->microseconds = microseconds;
}

DltFileIndexer::DltFileIndexer(QObject *parent) :
    QThread(parent)
{
    mode = modeIndexAndFilter;
    this->dltFile = NULL;
    this->pluginManager = NULL;
    defaultFilter = NULL;
    stopFlag = 0;

    pluginsEnabled = true;
    filtersEnabled = true;
    multithreaded = true;
    sortByTimeEnabled = false;

    maxRun = 0;
    currentRun = 0;
    msecsIndexCounter = 0;
    msecsFilterCounter = 0;
    msecsDefaultFilterCounter = 0;
}


DltFileIndexer::DltFileIndexer(QDltFile *dltFile, QDltPluginManager *pluginManager, QDltDefaultFilter *defaultFilter, QMainWindow *parent) :
    QThread(parent)
{
    mode = modeIndexAndFilter;
    this->dltFile = dltFile;
    this->pluginManager = pluginManager;
    this->defaultFilter = defaultFilter;
    stopFlag = 0;

    pluginsEnabled = true;
    filtersEnabled = true;
    multithreaded = true;
    sortByTimeEnabled = 0;

    maxRun = 0;
    currentRun = 0;
    msecsIndexCounter = 0;
    msecsFilterCounter = 0;
    msecsDefaultFilterCounter = 0;
}

DltFileIndexer::~DltFileIndexer()
{
}

bool DltFileIndexer::index(int num)
{
    QTime time;

    // start performance counter
    time.start();

    // load filter index if enabled
    if(!filterCache.isEmpty() && loadIndexCache(dltFile->getFileName(num)))
    {
        // loading index from filter is succesful
        qDebug() << "Loaded index cache for file" << dltFile->getFileName(num);
        msecsIndexCounter = time.elapsed();
        return true;
    }

    // prepare indexing
    //dltFile->clearIndex();
    QFile f(dltFile->getFileName(num));

    // open file
    if(!f.open(QIODevice::ReadOnly))
    {
        qWarning() << "Cannot open file in DltFileIndexer " << f.errorString();
        return false;
    }

    // check if file is empty
    if(f.size() == 0)
    {
        // No need to do anything here.
        f.close();
        return true;
    }

    // Initialise progress bar
    emit(progressText(QString("%1/%2").arg(currentRun).arg(maxRun)));
    emit(progressMax(f.size()));

    // clear old index
    indexAllList.clear();

    // Go through the segments and create new index
    char lastFound = 0;
    qint64 length;
    qint64 pos;
    char *data = new char[DLT_FILE_INDEXER_SEG_SIZE];
    do
    {

        pos = f.pos();
        length = f.read(data,DLT_FILE_INDEXER_SEG_SIZE);
        for(int num=0;num < length;num++)
        {
            if(data[num] == 'D')
            {
                lastFound = 'D';
            }
            else if(lastFound == 'D' && data[num] == 'L')
            {
                lastFound = 'L';
            }
            else if(lastFound == 'L' && data[num] == 'T')
            {
                lastFound = 'T';
            }
            else if(lastFound == 'T' && data[num] == 0x01)
            {
                indexAllList.append(pos+num-3);
                lastFound = 0;
            }
            else
            {
                lastFound = 0;
            }

            /* stop if requested */
            if(stopFlag)
            {
                delete[] data;
                f.close();
                return false;
            }
        }
        emit(progress(pos));
    }
    while(length>0);

    // delete buffer
    delete[] data;

    // close file
    f.close();

    qDebug() << "Created index for file" << dltFile->getFileName(num);

    // update performance counter
    msecsIndexCounter = time.elapsed();

    // write index if enabled
    if(!filterCache.isEmpty())
    {
        saveIndexCache(dltFile->getFileName(num));
        qDebug() << "Saved index cache for file" << dltFile->getFileName(num);
    }

    return true;
}

bool DltFileIndexer::indexFilter(QStringList filenames)
{
    QDltMsg msg;
    QDltPlugin *item;
    QDltFilterList filterList;
    QTime time;

    // start performance counter
    time.start();

    // get filter list
    filterList = dltFile->getFilterList();

    // load filter index, if enabled and not an initial loading of file
    if(!filterCache.isEmpty() && mode != modeIndexAndFilter && loadFilterIndexCache(filterList,indexFilterList,filenames))
    {
        // loading filter index from filter is succesful
        qDebug() << "Loaded filter index cache for files" << filenames;
        msecsFilterCounter = time.elapsed();
        return true;
    }

    // Initialise progress bar
    emit(progressText(QString("%1/%2").arg(currentRun).arg(maxRun)));
    emit(progressMax(dltFile->size()));

    // clear index filter
    indexFilterList.clear();
    indexFilterListSorted.clear();
    getLogInfoList.clear();

    indexFilterList.reserve(dltFile->size()); // make sure no reallocation of memory is needed to prevent memory issues

    // get silent mode
    bool silentMode = !OptManager::getInstance()->issilentMode();

    // run through all viewer plugins
    // must be run in the UI thread, if some gui actions are performed

    // run through all DLT messages
    for(int ix=0;ix<dltFile->size();ix++)
    {
        /* Fill message from file */
        if(!dltFile->getMsg(ix, msg))
        {
            /* Skip broken messages */
            continue;
        }

        /* check if it is a version messages and
           version string not already parsed */
        if((mode == modeIndexAndFilter) &&
           msg.getType()==QDltMsg::DltTypeControl &&
           msg.getSubtype()==QDltMsg::DltControlResponse &&
           msg.getCtrlServiceId() == DLT_SERVICE_ID_GET_SOFTWARE_VERSION)
        {
            QByteArray payload = msg.getPayload();
            QByteArray data = payload.mid(9,(payload.size()>262)?256:(payload.size()-9));
            QString version = msg.toAscii(data,true);
            version = version.trimmed(); // remove all white spaces at beginning and end
            versionString(msg.getEcuid(),version);
        }

        /* check if it is a timezone message */
        if((mode == modeIndexAndFilter) &&
           msg.getType()==QDltMsg::DltTypeControl &&
           msg.getSubtype()==QDltMsg::DltControlResponse &&
           msg.getCtrlServiceId() == DLT_SERVICE_ID_TIMEZONE)
        {
            QByteArray payload = msg.getPayload();
            if(payload.size() == sizeof(DltServiceTimezone))
            {
                DltServiceTimezone *service;
                service = (DltServiceTimezone*) payload.constData();

                if(msg.getEndianness() == QDltMsg::DltEndiannessLittleEndian)
                    timezone(service->timezone,service->isdst);
                else
                    timezone(DLT_SWAP_32(service->timezone),service->isdst);
            }
        }

        /* check if it is a timezone message */
        if((mode == modeIndexAndFilter) &&
           msg.getType()==QDltMsg::DltTypeControl &&
           msg.getSubtype()==QDltMsg::DltControlResponse &&
           msg.getCtrlServiceId() == DLT_SERVICE_ID_UNREGISTER_CONTEXT)
        {
            QByteArray payload = msg.getPayload();
            if(payload.size() == sizeof(DltServiceUnregisterContext))
            {
                DltServiceUnregisterContext *service;
                service = (DltServiceUnregisterContext*) payload.constData();

                unregisterContext(msg.getEcuid(),QString(QByteArray(service->apid,4)),QString(QByteArray(service->ctid,4)));
            }

        }

        /* Process all viewer plugins */
        if((mode == modeIndexAndFilter) && pluginsEnabled)
        {
            for(int ivp=0;ivp < activeViewerPlugins.size();ivp++)
            {
                item = (QDltPlugin*)activeViewerPlugins.at(ivp);
                item->initMsg(ix, msg);
            }
        }

        /* Process all decoderplugins */
        pluginManager->decodeMsg(msg,silentMode);

        /* Add to filterindex if matches */
        if(filterList.checkFilter(msg))
        {
            if(sortByTimeEnabled)
                indexFilterListSorted.insert(DltFileIndexerKey(msg.getTime(),msg.getMicroseconds()),ix);
            else
                indexFilterList.append(ix);
        }

        /* Offer messages again to viewer plugins after decode */
        if((mode == modeIndexAndFilter) && pluginsEnabled)
        {
            for(int ivp=0;ivp<activeViewerPlugins.size();ivp++)
            {
                item = (QDltPlugin *)activeViewerPlugins.at(ivp);
                item->initMsgDecoded(ix, msg);
            }
        }

        /* update context configuration when loading file */
        if( (mode == modeIndexAndFilter) &&
            msg.getType()==QDltMsg::DltTypeControl &&
            msg.getSubtype()==QDltMsg::DltControlResponse )
        {
            const char *ptr;
            int32_t length;
            uint32_t service_id=0, service_id_tmp=0;

            QByteArray payload = msg.getPayload();
            ptr = payload.constData();
            length = payload.size();
            DLT_MSG_READ_VALUE(service_id_tmp,ptr,length,uint32_t);
            service_id=DLT_ENDIAN_GET_32( ((msg.getEndianness()==QDltMsg::DltEndiannessBigEndian)?DLT_HTYP_MSBF:0), service_id_tmp);

            if(service_id == DLT_SERVICE_ID_GET_LOG_INFO)
            {
                getLogInfoList.append(ix);
            }
        }

        /* Update progress every 0.5% */
        if( 0 == (ix%1000))
        {
            emit(progress(ix));
        }

        /* stop if requested */
        if(stopFlag)
        {
            return false;
        }
    }

    // run through all viewer plugins
    // must be run in the UI thread, if some gui actions are performed

    qDebug() << "Created filter index for files" << filenames;

    // update performance counter
    msecsFilterCounter = time.elapsed();

    // use sorted values if sort by time enabled
    if(sortByTimeEnabled)
        indexFilterList = QVector<qint64>::fromList(indexFilterListSorted.values());

    // write filter index if enabled
    if(!filterCache.isEmpty())
    {
        saveFilterIndexCache(filterList,indexFilterList,filenames);
        qDebug() << "Saved filter index cache for files" << filenames;
    }

    return true;
}

bool DltFileIndexer::indexDefaultFilter()
{
    QDltMsg msg;
    QTime time;

    // start performance counter
    time.start();

    // Initialise progress bar
    emit(progressText(QString("%1/%2").arg(currentRun).arg(maxRun)));
    emit(progressMax(dltFile->size()));

    // clear all default filter cache index
    defaultFilter->clearFilterIndex();

    // get silent mode
    bool silentMode = !OptManager::getInstance()->issilentMode();

    /* run through the whole open file */
    for(int ix=0;ix<dltFile->size();ix++)
    {
        /* Fill message from file */
        if(!dltFile->getMsg(ix, msg))
        {
            /* Skip broken messages */
            continue;
        }

        /* Process all decoderplugins */
        pluginManager->decodeMsg(msg,silentMode);

        /* run through all default filter */
        for(int num=0;num<defaultFilter->defaultFilterList.size();num++)
        {
            QDltFilterList *filterList;
            filterList = defaultFilter->defaultFilterList[num];

            /* check if filter matches message */
            if(filterList->checkFilter(msg))
            {
                QDltFilterIndex *filterIndex;
                filterIndex = defaultFilter->defaultFilterIndex[num];

                /* if filter match add message to index cache */
                filterIndex->indexFilter.append(ix);

            }
        }

        /* Update progress every 0.5% */
        if( 0 == (ix%1000))
        {
            emit(progress(ix));
        }

        /* stop if requested */
        if(stopFlag)
        {
            return false;
        }
    }

    /* update plausibility checks of filter index cache, filename and filesize */
    for(int num=0;num<defaultFilter->defaultFilterIndex.size();num++)
    {
        QDltFilterIndex *filterIndex;
        QDltFilterList *filterList;
        filterIndex = defaultFilter->defaultFilterIndex[num];
        filterList = defaultFilter->defaultFilterList[num];

        filterIndex->setDltFileName(dltFile->getFileName());
        filterIndex->setAllIndexSize(dltFile->size());

        // write filter index if enabled
        if(!filterCache.isEmpty())
            saveFilterIndexCache(*filterList,filterIndex->indexFilter,QStringList(dltFile->getFileName()));
    }

    // update performance counter
    msecsDefaultFilterCounter = time.elapsed();

    return true;
}


void DltFileIndexer::lock()
{
    indexLock.lock();
}

void DltFileIndexer::unlock()
{
    indexLock.unlock();
}

bool DltFileIndexer::tryLock()
{
    return indexLock.tryLock();
}

void DltFileIndexer::run()
{
    // initialise stop flag
    stopFlag = false;

    // clear performance counter
    msecsIndexCounter = 0;
    msecsFilterCounter = 0;
    msecsDefaultFilterCounter = 0;

    // get all active plugins
    activeViewerPlugins = pluginManager->getViewerPlugins();
    activeDecoderPlugins = pluginManager->getDecoderPlugins();

    // calculate runs
    if(mode == modeIndexAndFilter)
        maxRun = dltFile->getNumberOfFiles()+1;
    else
        maxRun = 1;
    currentRun = 1;

    // index
    if(mode == modeIndexAndFilter)
    {
        for(int num=0;num<dltFile->getNumberOfFiles();num++)
        {
            if(!index(num))
            {
                // error
                return;
            }
            dltFile->setDltIndex(indexAllList,num);
            currentRun++;
        }
        emit(finishIndex());
    }
    else if(mode == modeNone)
    {
        // only update view
        emit(finishIndex());
    }

    // indexFilter
    if(mode == modeIndexAndFilter || mode == modeFilter)
    {
        QStringList filenames;
        for(int num=0;num<dltFile->getNumberOfFiles();num++)
            filenames.append(QFileInfo(dltFile->getFileName(num)).fileName());
        if((mode != modeNone) && !indexFilter(filenames))
        {
            // error
            return;
        }
        dltFile->setIndexFilter(indexFilterList);
        emit(finishFilter());
    }

    // indexDefaultFilter
    if(mode == modeDefaultFilter)
    {
        if(!indexDefaultFilter())
        {
            // error
            return;
        }
        emit(finishDefaultFilter());
    }

    // print performance counter
    QTime time;
    time = QTime(0,0);time = time.addMSecs(msecsIndexCounter);
    qDebug() << "Duration Indexing:" << time.toString("hh:mm:ss.zzz") << "msecs";
    time = QTime(0,0);time = time.addMSecs(msecsFilterCounter);
    qDebug() << "Duration Filter Indexing:" << time.toString("hh:mm:ss.zzz") << "msecs";
    time = QTime(0,0);time = time.addMSecs(msecsDefaultFilterCounter);
    qDebug() << "Duration Default Filter Indexing:" << time.toString("hh:mm:ss.zzz") << "msecs";

}

void DltFileIndexer::stop()
{
    // stop the thread
    stopFlag = true;

    while(isRunning())
    {
        // wait until task is not running anymore
        usleep(100000);
    }
}

// load/safe index from/to file
bool DltFileIndexer::loadIndexCache(QString filename)
{
    QString filenameCache;

    // check if caching is enabled
    if(filterCache.isEmpty())
        return false;

    // get the filename for the cache file
    filenameCache = filenameIndexCache(filename);

    // load the cache file
    if(!loadIndex(filterCache + "/" +filenameCache,indexAllList))
    {
        // loading cache file failed
        return false;
    }

    return true;
}

bool DltFileIndexer::saveIndexCache(QString filename)
{
    QString filenameCache;

    // check if caching is enabled
    if(filterCache.isEmpty())
        return false;

    // get the filename for the cache file
    filenameCache = filenameIndexCache(filename);

    // save the cache file
    if(!saveIndex(filterCache + "/" +filenameCache,indexAllList))
    {
        // saving cache file failed
        return false;
    }

    return true;
}

QString DltFileIndexer::filenameIndexCache(QString filename)
{
    QString hashString;
    QByteArray hashByteArray;
    QByteArray md5;
    QString filenameCache;

    // create string to be hashed
    hashString = QFileInfo(filename).fileName();
    hashString += "_" + QString("%1").arg(dltFile->fileSize());

    // create byte array from hash string
    hashByteArray = hashString.toLatin1();

    // create MD5 from byte array
    md5 = QCryptographicHash::hash(hashByteArray, QCryptographicHash::Md5);

    // create filename
    filenameCache = QString(md5.toHex())+".dix";

    qDebug() << filename << ">>" << filenameCache;

    return filenameCache;
}

// read/write index cache
bool DltFileIndexer::loadFilterIndexCache(QDltFilterList &filterList, QVector<qint64> &index, QStringList filenames)
{
    QString filenameCache;

    // check if caching is enabled
    if(filterCache.isEmpty())
        return false;

    // get the filename for the cache file
    filenameCache = filenameFilterIndexCache(filterList,filenames);

    // load the cache file
    if(!loadIndex(filterCache + "/" +filenameCache,index))
    {
        // loading of cache file failed
        return false;
    }


    return true;
}

bool DltFileIndexer::saveFilterIndexCache(QDltFilterList &filterList, QVector<qint64> index, QStringList filenames)
{
    QString filename;

    // check if caching is enabled
    if(filterCache.isEmpty())
        return false;

    // get the filename for the cache file
    filename = filenameFilterIndexCache(filterList,filenames);

    // save the cache file
    if(!saveIndex(filterCache + "/" +filename,index))
    {
        // saving of cache file failed
        return false;
    }

    return true;
}

QString DltFileIndexer::filenameFilterIndexCache(QDltFilterList &filterList,QStringList filenames)
{
    QString hashString;
    QByteArray hashByteArray;
    QByteArray md5;
    QByteArray md5FilterList;
    QString filename;

    // get filter list
    md5FilterList = filterList.createMD5();

    // create string to be hashed
    if(sortByTimeEnabled)
        filenames.sort();
    hashString = filenames.join(QString("_"));
    hashString += "_" + QString("%1").arg(dltFile->fileSize());

    // create byte array from hash string
    hashByteArray = hashString.toLatin1();

    // create MD5 from byte array
    md5 = QCryptographicHash::hash(hashByteArray, QCryptographicHash::Md5);

    // create filename
    if(sortByTimeEnabled)
        filename = QString(md5.toHex())+"_"+QString(md5FilterList.toHex())+"_S.dix";
    else
        filename = QString(md5.toHex())+"_"+QString(md5FilterList.toHex())+".dix";

    qDebug() << filenames << ">>" << filename;

    return filename;
}

bool DltFileIndexer::saveIndex(QString filename, const QVector<qint64> &index)
{
    quint32 version = DLT_FILE_INDEXER_FILE_VERSION;
    qint64 value;

    QFile file(filename);

    // open cache file
    if(!file.open(QFile::WriteOnly))
    {
        // open file failed
        return false;
    }

    // write version
    file.write((char*)&version,sizeof(version));

    // write complete index
    for(int num=0;num<index.size();num++)
    {
        value = index[num];
        file.write((char*)&value,sizeof(value));
    }

    // close cache file
    file.close();

    return true;
}

bool DltFileIndexer::loadIndex(QString filename, QVector<qint64> &index)
{
    quint32 version;
    qint64 value;
    int length;

    QFile file(filename);

    index.clear();

    // open cache file
    if(!file.open(QFile::ReadOnly))
    {
        // open file failed
        return false;
    }

    // read version
    length = file.read((char*)&version,sizeof(version));

    // compare version if valid
    if((length != sizeof(version)) || version != DLT_FILE_INDEXER_FILE_VERSION)
    {
        // wrong version number
        file.close();
        return false;
    }

    // read complete index
    do
    {
        length = file.read((char*)&value,sizeof(value));
        if(length==sizeof(value))
            index.append(value);
    }
    while(length==sizeof(value));

    // close cache file
    file.close();

    return true;
}
