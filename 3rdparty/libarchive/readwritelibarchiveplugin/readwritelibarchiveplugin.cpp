/*
* Copyright (C) 2019 ~ 2020 Uniontech Software Technology Co.,Ltd.
*
* Author:     chendu <chendu@uniontech.com>
*
* Maintainer: chendu <chendu@uniontech.com>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "readwritelibarchiveplugin.h"
#include "datamanager.h"

#include <QFileInfo>
#include <QThread>
#include <QDir>
#include <QDebug>
#include <QDirIterator>
#include <QTemporaryDir>

#include <archive_entry.h>

// 300M
#define MB300 314572800 /*(300*1024*1024)*/

ReadWriteLibarchivePluginFactory::ReadWriteLibarchivePluginFactory()
{
    registerPlugin<ReadWriteLibarchivePlugin>();
}
ReadWriteLibarchivePluginFactory::~ReadWriteLibarchivePluginFactory()
{

}

ReadWriteLibarchivePlugin::ReadWriteLibarchivePlugin(QObject *parent, const QVariantList &args)
    : LibarchivePlugin(parent, args)
//    , m_archiveReadDisk(archive_read_disk_new())
{
    qDebug() << "ReadWriteLibarchivePlugin";
    m_archiveReadDisk.reset(archive_read_disk_new());
    archive_read_disk_set_standard_lookup(m_archiveReadDisk.data());
}

ReadWriteLibarchivePlugin::~ReadWriteLibarchivePlugin()
{
}

PluginFinishType ReadWriteLibarchivePlugin::addFiles(const QList<FileEntry> &files, const CompressOptions &options)
{
    const bool creatingNewFile = !QFileInfo::exists(m_strArchiveName); //true:压缩 false:追加
    m_writtenFilesSet.clear();

    if (!creatingNewFile && !initializeReader()) {
        return PFT_Error;
    }

    if (!initializeWriter(creatingNewFile, options)) {
        return PFT_Error;
    }

    // 1.First write the new files.
    m_currentAddFilesSize = 0;
    qlonglong totalSize = options.qTotalSize;

    for (FileEntry selectedFile : files) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            break;
        }

        emit signalCurFileName(selectedFile.strFullPath);

        if (!creatingNewFile && !selectedFile.isDirectory) { //追加操作更新压缩包总大小，计算进度使用
            totalSize += selectedFile.qSize;
        }

        if (!writeFileFromEntry(selectedFile.strFullPath, options.strDestination, selectedFile, totalSize)) {
            finish(false);
            return PFT_Error;
        }

        // 2.For directories, write all subfiles/folders.
        const QString &fullPath = selectedFile.strFullPath;
        if (QFileInfo(fullPath).isDir()) {
            QDirIterator it(fullPath,
                            QDir::AllEntries | QDir::Readable |
                            QDir::Hidden | QDir::NoDotAndDotDot,
                            QDirIterator::Subdirectories);
            QString firstDirFullPath = fullPath;
            QString externalPath = QFileInfo(fullPath).path() + QDir::separator();

            while (!QThread::currentThread()->isInterruptionRequested() && it.hasNext()) {
                QString path = it.next();

                emit signalCurFileName(it.filePath());

                if ((it.fileName() == QLatin1String("..")) ||
                        (it.fileName() == QLatin1String("."))) {
                    continue;
                }

                if (!creatingNewFile && !it.fileInfo().isDir()) { //追加操作更新压缩包总大小，计算进度使用
                    totalSize += it.fileInfo().size();
                }

                if (!writeFileTodestination(path, options.strDestination, externalPath, totalSize)) {
                    finish(false);
                    return PFT_Error;
                }
            }
        }
    }

    bool isSuccessful = true;
    // 3.If we have old archive entries.
    if (!creatingNewFile) {
        isSuccessful = processOldEntries_Add(totalSize);
    }

    finish(isSuccessful);

    emit signalFinished(isSuccessful ? PFT_Nomral : PFT_Error);

    return isSuccessful ? PFT_Nomral : PFT_Error;
}

PluginFinishType ReadWriteLibarchivePlugin::deleteFiles(const QList<FileEntry> &files)
{
    if (files.count() == 0) {
        return PFT_Error;
    }
    if (!initializeReader()) {
        return PFT_Error;
    }

    if (!initializeWriter()) {
        return PFT_Error;
    }

    m_currentExtractedFilesSize = 0; //初始化，删除进度使用

    const bool isSuccessful = deleteEntry(files);

    finish(isSuccessful);
    return isSuccessful ? PFT_Nomral : PFT_Error;
}

bool ReadWriteLibarchivePlugin::initializeWriter(const bool creatingNewFile, const CompressOptions &options)
{
    m_tempFile.setFileName(m_strArchiveName);
    if (!m_tempFile.open(QIODevice::WriteOnly | QIODevice::Unbuffered)) {
        emit error(("Failed to create a temporary file for writing data."));
        return false;
    }

    m_archiveWriter.reset(archive_write_new());
    if (!(m_archiveWriter.data())) {
        emit error(("The archive writer could not be initialized."));
        return false;
    }

    QString mimeTypeName = m_mimetype.name();

    // pax_restricted is the libarchive default, let's go with that.
    if (mimeTypeName == "application/zip") {
        archive_write_set_format_zip(m_archiveWriter.data());
    } else {
        archive_write_set_format_pax_restricted(m_archiveWriter.data());
    }


    if (creatingNewFile) {
        if (!initializeNewFileWriterFilters(options)) {
            return false;
        }
    } else {
        if (!initializeWriterFilters()) {
            return false;
        }
    }

    if (archive_write_open_fd(m_archiveWriter.data(), m_tempFile.handle()) != ARCHIVE_OK) {
        emit error(("Could not open the archive for writing entries."));
        return false;
    }

    return true;
}

bool ReadWriteLibarchivePlugin::initializeWriterFilters()
{
    int ret;
    bool requiresExecutable = false;
    switch (archive_filter_code(m_archiveReader.data(), 0)) {
    case ARCHIVE_FILTER_GZIP:
        ret = archive_write_add_filter_gzip(m_archiveWriter.data());
        break;
    case ARCHIVE_FILTER_BZIP2:
        ret = archive_write_add_filter_bzip2(m_archiveWriter.data());
        break;
    case ARCHIVE_FILTER_XZ:
        ret = archive_write_add_filter_xz(m_archiveWriter.data());
        break;
    case ARCHIVE_FILTER_LZMA:
        ret = archive_write_add_filter_lzma(m_archiveWriter.data());
        break;
    case ARCHIVE_FILTER_COMPRESS:
        ret = archive_write_add_filter_compress(m_archiveWriter.data());
        break;
    case ARCHIVE_FILTER_LZIP:
        ret = archive_write_add_filter_lzip(m_archiveWriter.data());
        break;
    case ARCHIVE_FILTER_LZOP:
        ret = archive_write_add_filter_lzop(m_archiveWriter.data());
        requiresExecutable = true;   //add
        break;
    case ARCHIVE_FILTER_LRZIP:
        ret = archive_write_add_filter_lrzip(m_archiveWriter.data());
        requiresExecutable = true;
        break;
    case ARCHIVE_FILTER_LZ4:
        ret = archive_write_add_filter_lz4(m_archiveWriter.data());
        break;
#ifdef HAVE_ZSTD_SUPPORT
    case ARCHIVE_FILTER_ZSTD:
        ret = archive_write_add_filter_zstd(m_archiveWriter.data());
        break;
#endif
    case ARCHIVE_FILTER_NONE:
        ret = archive_write_add_filter_none(m_archiveWriter.data());
        break;
    default:
        return false;
    }

    // Libarchive emits a warning for lrzip due to using external executable.
    if ((requiresExecutable && ret != ARCHIVE_WARN) ||
            (!requiresExecutable && ret != ARCHIVE_OK)) {
        emit error(("Could not set the compression method."));
        return false;
    }

    return true;
}

bool ReadWriteLibarchivePlugin::initializeNewFileWriterFilters(const CompressOptions &options)
{
    int ret = ARCHIVE_OK;
    bool requiresExecutable = false;
    if (m_strArchiveName.right(2).toUpper() == QLatin1String("GZ")) {
        ret = archive_write_add_filter_gzip(m_archiveWriter.data());
    } else if (m_strArchiveName.right(3).toUpper() == QLatin1String("BZ2")) {
        ret = archive_write_add_filter_bzip2(m_archiveWriter.data());
    } else if (m_strArchiveName.right(2).toUpper() == QLatin1String("XZ")) {
        ret = archive_write_add_filter_xz(m_archiveWriter.data());
    } else if (m_strArchiveName.right(4).toUpper() == QLatin1String("LZMA")) {
        ret = archive_write_add_filter_lzma(m_archiveWriter.data());
    } else if (m_strArchiveName.right(2).toUpper() == QLatin1String(".Z")) {
        ret = archive_write_add_filter_compress(m_archiveWriter.data());
    } else if (m_strArchiveName.right(2).toUpper() == QLatin1String("LZ")) {
        ret = archive_write_add_filter_lzip(m_archiveWriter.data());
    } else if (m_strArchiveName.right(3).toUpper() == QLatin1String("LZO")) {
        ret = archive_write_add_filter_lzop(m_archiveWriter.data());
        requiresExecutable = true;
    } else if (m_strArchiveName.right(3).toUpper() == QLatin1String("LRZ")) {
        ret = archive_write_add_filter_lrzip(m_archiveWriter.data());
        requiresExecutable = true;
    } else if (m_strArchiveName.right(3).toUpper() == QLatin1String("LZ4")) {
        ret = archive_write_add_filter_lz4(m_archiveWriter.data());
    } else if (m_strArchiveName.right(3).toUpper() == QLatin1String("TAR")) {
        ret = archive_write_add_filter_none(m_archiveWriter.data());
    } else if (m_strArchiveName.right(3).toUpper() == QLatin1String("GZIP")) {
        ret = archive_write_add_filter_gzip(m_archiveWriter.data());
    }

    // Libarchive emits a warning for lrzip due to using external executable.
    if ((requiresExecutable && ret != ARCHIVE_WARN) ||
            (!requiresExecutable && ret != ARCHIVE_OK)) {
        emit error(("Could not set the compression method."));
        return false;
    }

    // Set compression level if passed in CompressionOptions.
    if (options.iCompressionLevel != -1) {
        if (m_strArchiveName.right(3).toUpper() == QLatin1String("ZIP")) {
            ret = archive_write_set_options(m_archiveWriter.data(), QString("compression-level=" + QString::number(options.iCompressionLevel)).toUtf8().constData());
        } else {
            ret = archive_write_set_filter_option(m_archiveWriter.data(), nullptr, "compression-level", QString::number(options.iCompressionLevel).toUtf8().constData());
        }


        if (ret != ARCHIVE_OK) {
            emit error(("Could not set the compression level."));
            return false;
        }
    }

    if (false == options.strPassword.isEmpty()) {
        archive_write_set_options(m_archiveWriter.data(), "encryption=aes256");
        archive_write_set_passphrase(m_archiveWriter.data(), options.strPassword.toUtf8().constData());
    }


    return true;
}

void ReadWriteLibarchivePlugin::finish(const bool isSuccessful)
{
    if (!isSuccessful || QThread::currentThread()->isInterruptionRequested()) {
        archive_write_fail(m_archiveWriter.data());
        m_tempFile.cancelWriting();
    } else {
        // archive_write_close() needs to be called before calling QSaveFile::commit(),
        // otherwise the latter will close() the file descriptor m_archiveWriter is still working on.
        // TODO: We need to abstract this code better so that we only deal with one
        // object that manages both QSaveFile and ArchiveWriter.
        archive_write_close(m_archiveWriter.data());
        m_tempFile.commit();
    }
}

bool ReadWriteLibarchivePlugin::writeFileTodestination(const QString &sourceFileFullPath, const QString &destination, const QString &externalPath,  const qlonglong &totalsize)
{
    //如果是文件夹，采用软链接的形式
    QString newFilePath = sourceFileFullPath;
    QFileInfo sourceFileInfo(sourceFileFullPath);
    QString absoluteDestinationPath = "";
    if (sourceFileInfo.isDir()) {
        QScopedPointer<QTemporaryDir> extractTempDir;
        extractTempDir.reset(new QTemporaryDir());
        absoluteDestinationPath = extractTempDir->path() + QDir::separator() + destination;
        QDir dir;
        dir.mkpath(absoluteDestinationPath);
        QString newFilePath = absoluteDestinationPath + sourceFileInfo.fileName();
        if (QFile::link(sourceFileFullPath, newFilePath)) {
//            qDebug() << "Symlink's created:" << destination << sourceFileFullPath;
        } else {
            qDebug() << "Can't create symlink" << destination << sourceFileFullPath;
            return false;
        }
    }

//    QFileInfo fileInfo(relativeName);
    QFileInfo fileInfo(newFilePath);
    QString absoluteFilename = fileInfo.isSymLink() ? fileInfo.symLinkTarget() : fileInfo.absoluteFilePath();
    QString destinationFilename = absoluteFilename;
    destinationFilename = destination + newFilePath.remove(externalPath);

    // #253059: Even if we use archive_read_disk_entry_from_file,
    //          libarchive may have been compiled without HAVE_LSTAT,
    //          or something may have caused it to follow symlinks, in
    //          which case stat() will be called. To avoid this, we
    //          call lstat() ourselves.
    struct stat st;
    lstat(QFile::encodeName(absoluteFilename).constData(), &st); // krazy:exclude=syscalls

    struct archive_entry *entry = archive_entry_new();
    archive_entry_set_pathname(entry, QFile::encodeName(destinationFilename).constData());
    archive_entry_copy_sourcepath(entry, QFile::encodeName(absoluteFilename).constData());
    archive_read_disk_entry_from_file(m_archiveReadDisk.data(), entry, -1, &st);

    const auto returnCode = archive_write_header(m_archiveWriter.data(), entry);
    if (returnCode == ARCHIVE_OK) {
        // If the whole archive is extracted and the total filesize is
        // available, we use partial progress.
        copyData(absoluteFilename, m_archiveWriter.data(), totalsize);
        if (sourceFileInfo.isDir()) {
            QDir::cleanPath(absoluteDestinationPath);
        }
    } else {

        emit error(("Could not compress entry."));

        archive_entry_free(entry);

        if (sourceFileInfo.isDir()) {
            QDir::cleanPath(absoluteDestinationPath);
        }
        return false;
    }

    if (QThread::currentThread()->isInterruptionRequested()) {
        archive_entry_free(entry);
        return false;
    }

    m_writtenFilesSet.insert(destinationFilename);
//    emitEntryFromArchiveEntry(entry);//屏蔽by hsw 20200528

    archive_entry_free(entry);

    return true;
}

bool ReadWriteLibarchivePlugin::writeFileFromEntry(const QString &relativeName, const QString destination, FileEntry &pEntry, const qlonglong &totalsize)
{
    Q_UNUSED(pEntry)
    //如果是文件夹，采用软链接的形式
    QString newFilePath = relativeName;
    QString absoluteDestinationPath = "";
    QFileInfo relativeFileInfo(relativeName);

    if (relativeFileInfo.isDir()) {
        QScopedPointer<QTemporaryDir> extractTempDir;
        extractTempDir.reset(new QTemporaryDir());
        absoluteDestinationPath = extractTempDir->path() + QDir::separator() + destination;
        QDir dir;
        dir.mkpath(absoluteDestinationPath);//创建临时文件夹
        QString newFilePath = absoluteDestinationPath + relativeFileInfo.fileName();
        if (QFile::link(relativeName, newFilePath)) {
//            qDebug() << "Symlink's created:" << destination << relativeName;
        } else {
            qDebug() << "Can't create symlink" << destination << relativeName;
            return false;
        }
    }

    QFileInfo fileInfo(newFilePath);
    const QString absoluteFilename = fileInfo.isSymLink() ? fileInfo.symLinkTarget() : fileInfo.absoluteFilePath();
    const QString destinationFilename = destination + fileInfo.fileName();

    // #253059: Even if we use archive_read_disk_entry_from_file,
    //          libarchive may have been compiled without HAVE_LSTAT,
    //          or something may have caused it to follow symlinks, in
    //          which case stat() will be called. To avoid this, we
    //          call lstat() ourselves.
    struct stat st;
    lstat(QFile::encodeName(absoluteFilename).constData(), &st); // krazy:exclude=syscalls

    struct archive_entry *entry = archive_entry_new();
    archive_entry_set_pathname(entry, QFile::encodeName(destinationFilename).constData());
    archive_entry_copy_sourcepath(entry, QFile::encodeName(absoluteFilename).constData());
    archive_read_disk_entry_from_file(m_archiveReadDisk.data(), entry, -1, &st);

    const auto returnCode = archive_write_header(m_archiveWriter.data(), entry);
    if (returnCode == ARCHIVE_OK) {
        // If the whole archive is extracted and the total filesize is
        // available, we use partial progress.
        copyData(absoluteFilename, m_archiveWriter.data(), totalsize);
        if (QFileInfo(relativeName).isDir()) {//clean temp path;
            QDir::cleanPath(absoluteDestinationPath);
        }
    } else {
        if (QFileInfo(relativeName).isDir()) {
            QDir::cleanPath(absoluteDestinationPath);
        }
        emit error(("Could not compress entry."));

        archive_entry_free(entry);

        return false;
    }

    if (QThread::currentThread()->isInterruptionRequested()) {
        archive_entry_free(entry);
        return false;
    }

    m_writtenFilesSet.insert(destinationFilename);
    //    emitEntryFromArchiveEntry(entry);//这句不需要添加，因为在MainWindow::addArchive函数中已经建立了Archive::Entry在ArchiveModel的树

    archive_entry_free(entry);

    return true;
}

void ReadWriteLibarchivePlugin::copyData(const QString &filename, archive *dest, const qlonglong &totalsize, bool bInternalDuty)
{
    Q_UNUSED(bInternalDuty)
//    m_currentExtractedFilesSize = 0;
    char buff[10240];
    QFile file(filename);

    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    //static int pastProgress = -1;

    //pastProgress = -1;

    //float fileSize = static_cast<float>(file.size());//filesize in the disk

    auto readBytes = file.read(buff, sizeof(buff));
    while (readBytes > 0 && !QThread::currentThread()->isInterruptionRequested()) {
        if (m_bPause) { //压缩暂停
            sleep(1);
//            qDebug() << "pause";
            continue;
        }

        archive_write_data(dest, buff, static_cast<size_t>(readBytes));
        if (archive_errno(dest) != ARCHIVE_OK) {
            file.close();
            return;
        }

//        if (bInternalDuty) {
        m_currentAddFilesSize += readBytes;
//            float currentProgress = (static_cast<float>(m_currentExtractedFilesSize) / fileSize) * info.fileProgressProportion + info.fileProgressStart;//根据内容写入比例，加上上次的进度值
//            if (static_cast<int>(100 * currentProgress) != pastProgress) {
//                emit progress(static_cast<double>(currentProgress));
//                pastProgress = static_cast<int>(100 * currentProgress);
//            }
//            //emit progress_filename(file.fileName());
//        }
        emit signalprogress((double(m_currentAddFilesSize)) / totalsize * 100);
        readBytes = file.read(buff, sizeof(buff));
    }

    file.close();
}

void ReadWriteLibarchivePlugin::copyDataFromSourceAdd(archive *source, archive *dest, const qlonglong &totalsize)
{
    char buff[10240];
    auto readBytes = archive_read_data(source, buff, sizeof(buff));

    while (readBytes > 0 && !QThread::currentThread()->isInterruptionRequested()) {
        archive_write_data(dest, buff, static_cast<size_t>(readBytes));
        if (archive_errno(dest) != ARCHIVE_OK) {
            return;
        }

        m_currentAddFilesSize += readBytes;
        emit signalprogress((double(m_currentAddFilesSize)) / totalsize * 100);

        readBytes = archive_read_data(source, buff, sizeof(buff));
    }
}

bool ReadWriteLibarchivePlugin::deleteEntry(const QList<FileEntry> &files)
{
    struct archive_entry *entry;
    archive_filter_count(m_archiveReader.data());
    ArchiveData &stArchiveData = DataManager::get_instance().archiveData();
    qlonglong totalSize = stArchiveData.qSize; // 原压缩包内文件总大小，供libarchive追加进度使用

    while (!QThread::currentThread()->isInterruptionRequested() && archive_read_next_header(m_archiveReader.data(), &entry) == ARCHIVE_OK) {
//        const QString file = QFile::decodeName(archive_entry_pathname(entry));
        const char *name = archive_entry_pathname(entry);
        QString entryName = m_common->trans2uft8(name, m_mapCode[QString(name)]); //该条entry在压缩包内文件名(全路径)
        bool flag = false;
        foreach (const FileEntry &tmpFileEntry, files) {
            if (tmpFileEntry.isDirectory) { //跳过该文件夹以及子文件
                if (entryName.startsWith(tmpFileEntry.strFullPath)) {
                    archive_read_data_skip(m_archiveReader.data()); //跳过该文件
                    totalSize -= static_cast<qlonglong>(archive_entry_size(entry));
                    flag = true;

                    emit signalCurFileName(entryName);
                    break;
                }
            } else {
                if (0 == entryName.compare(tmpFileEntry.strFullPath)) {
                    archive_read_data_skip(m_archiveReader.data()); //跳过该文件
                    totalSize -= static_cast<qlonglong>(archive_entry_size(entry));
                    flag = true;

                    emit signalCurFileName(entryName);
                    break;
                }
            }
        }

        // If entry is found, skip entry.
        if (flag) {
            continue;
        }


        // Write old entries.
        // 复制保留文件的数据到新的压缩包
        if (!writeEntryDelete(entry, totalSize)) {
            return false;
        }
    }

    return !QThread::currentThread()->isInterruptionRequested();
}

bool ReadWriteLibarchivePlugin::writeEntryDelete(struct archive_entry *entry, const qlonglong &totalSize)
{
    const int returnCode = archive_write_header(m_archiveWriter.data(), entry);
    switch (returnCode) {
    case ARCHIVE_OK:
        // If the whole archive is extracted and the total filesize is
        // available, we use partial progress.
        copyDataFromSource(m_archiveReader.data(), m_archiveWriter.data(), totalSize);
        break;
    case ARCHIVE_FAILED:
    case ARCHIVE_FATAL:
        emit error(("Could not compress entry, operation aborted."));
        return false;
    default:
        break;
    }

    return true;
}

bool ReadWriteLibarchivePlugin::writeEntryAdd(archive_entry *entry, const qlonglong &totalSize)
{
    const int returnCode = archive_write_header(m_archiveWriter.data(), entry);
    switch (returnCode) {
    case ARCHIVE_OK:
        // If the whole archive is extracted and the total filesize is
        // available, we use partial progress.
        copyDataFromSourceAdd(m_archiveReader.data(), m_archiveWriter.data(), totalSize);
        break;
    case ARCHIVE_FAILED:
    case ARCHIVE_FATAL:
        emit error(("Could not compress entry, operation aborted."));
        return false;
    default:
        break;
    }

    return true;
}

bool ReadWriteLibarchivePlugin::processOldEntries_Add(qlonglong &totalSize)
{
    struct archive_entry *entry;

    while (!QThread::currentThread()->isInterruptionRequested() && archive_read_next_header(m_archiveReader.data(), &entry) == ARCHIVE_OK) {
        const QString file = QFile::decodeName(archive_entry_pathname(entry));
        /** 判断原压缩包里文件是否与追加文件重复
          * 是：跳过该文件
          * 否：保留该文件，并将改文件数据复制新压缩包中
          */
        if (m_writtenFilesSet.contains(file.endsWith('/') ? file.chopped(1) : file)) {
            archive_read_data_skip(m_archiveReader.data());
            totalSize -= static_cast<qlonglong>(archive_entry_size(entry)); // 压缩包原文件总大小-被追加文件覆盖的文件大小

            continue;
        }
        emit signalCurFileName(file);

        // Write old entries.
        if (!writeEntryAdd(entry, totalSize)) {
            return false;
        }
    }

    return !QThread::currentThread()->isInterruptionRequested();
}
