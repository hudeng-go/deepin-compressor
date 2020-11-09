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
#ifndef READWRITELIBARCHIVEPLUGIN_H
#define READWRITELIBARCHIVEPLUGIN_H

#include "libarchiveplugin.h"
#include "kpluginfactory.h"

#include <QSaveFile>

#include <archive.h>

class ReadWriteLibarchivePluginFactory : public KPluginFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.KPluginFactory" FILE "kerfuffle_libarchive.json")
    Q_INTERFACES(KPluginFactory)
public:
    explicit ReadWriteLibarchivePluginFactory();
    ~ReadWriteLibarchivePluginFactory();
};

class ReadWriteLibarchivePlugin : public LibarchivePlugin
{
public:
    explicit ReadWriteLibarchivePlugin(QObject *parent, const QVariantList &args);
    ~ReadWriteLibarchivePlugin() override;

    // ReadOnlyArchiveInterface interface
public:
//    bool testArchive() override;
//    bool extractFiles(const QList<FileEntry> &files, const QString &destinationDirectory, const ExtractionOptions &options) override;

    // ReadWriteArchiveInterface interface
public:
    PluginFinishType addFiles(const QList<FileEntry> &files, const CompressOptions &options) override;
//    bool moveFiles(const QList<FileEntry> &files, const QString &strDestination, const CompressOptions &options) override;
//    bool copyFiles(const QList<FileEntry> &files, const QString &strDestination, const CompressOptions &options) override;
    /**
     * @brief deleteFiles 删除压缩包内指定文件
     * 删除流程:
     *  1.初始化
     *  2.筛选是否是需要删除的文件
     *  3.是:跳过; 否: 复制保留文件数据到新的压缩包中
     *  4.更新存放压缩包信息的map
     *  5.替换原压缩包
     * @param files 选中的文件
     * @return
     */
    PluginFinishType deleteFiles(const QList<FileEntry> &files) override;
//    bool addComment(const QString &comment) override;

private:
    QSaveFile m_tempFile;
    QStringList m_writtenFiles; //已经压缩完的文件
    ArchiveWrite m_archiveWriter;
    qlonglong m_currentAddFilesSize = 0;//当前已经压缩的文件大小（能展示出来的都已经压缩）

    bool initializeWriter(const bool creatingNewFile = false, const CompressOptions &options = CompressOptions());
    /**
     * @brief initializeWriterFilters 设置过滤器(追加)
     * @return
     */
    bool initializeWriterFilters();
    /**
     * @brief initializeNewFileWriterFilters 设置过滤器(压缩)
     * @param options
     * @return
     */
    bool initializeNewFileWriterFilters(const CompressOptions &options);
    void finish(const bool isSuccessful);
    /**
     * @brief writeFileTodestination 将文件夹内文件(夹)写入压缩包
     * @param sourceFileFullPath
     * @param destination
     * @param externalPath 文件夹路径
     * @param totalsize 原文件总大小
     * @param partialprogress
     * @return
     */
    bool writeFileTodestination(const QString &sourceFileFullPath, const QString &destination, const QString &externalPath, const qlonglong &totalsize, bool partialprogress = false);
    /**
     * @brief writeFileFromEntry 将文件写入压缩包
     * @param relativeName 本地文件(夹)(全路径)
     * @param destination 压缩包内路径
     * @param pEntry
     * @param totalsize 原文件总大小
     * @param bInternalDuty
     * @return
     */
    bool writeFileFromEntry(const QString &relativeName, const QString destination, FileEntry &pEntry, const qlonglong &totalsize, bool bInternalDuty = false);
    /**
     * @brief copyData 本地数据写入压缩包
     * @param filename 本地文件
     * @param dest 目标压缩包
     * @param totalsize 原文件总大小
     * @param bInternalDuty
     */
    void copyData(const QString &filename, struct archive *dest, const qlonglong &totalsize, bool bInternalDuty = true);
    /**
     * @brief deleteEntry 具体删除操作
     * 筛选是否是需要删除的文件，复制保留文件数据到新的压缩包中
     * @param files 选中的文件
     * @return
     */
    bool deleteEntry(const QList<FileEntry> &files);
    /**
     * @brief writeEntry 复制压缩包数据
     * @param entry
     * @return
     */
    bool writeEntry(struct archive_entry *entry);
};



#endif // READWRITELIBARCHIVEPLUGIN_H
