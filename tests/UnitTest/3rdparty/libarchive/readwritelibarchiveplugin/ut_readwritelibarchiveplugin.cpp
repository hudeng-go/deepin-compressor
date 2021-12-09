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

#include <gtest/gtest.h>
#include <gtest/src/stub.h>
#include "readwritelibarchiveplugin.h"

#include <QMimeDatabase>
#include <QFileInfo>


Q_DECLARE_METATYPE(KPluginMetaData)

class UT_ReadWriteLibarchivePluginFactory : public QObject, public ::testing::Test
{
public:
    UT_ReadWriteLibarchivePluginFactory(): m_tester(nullptr) {}

public:
    virtual void SetUp()
    {
        m_tester = new ReadWriteLibarchivePluginFactory();
    }

    virtual void TearDown()
    {
        delete m_tester;
    }

protected:
    ReadWriteLibarchivePluginFactory *m_tester;
};

class UT_ReadWriteLibarchivePlugin : public QObject, public ::testing::Test
{
public:
    UT_ReadWriteLibarchivePlugin(): m_tester(nullptr) {}

public:
    virtual void SetUp()
    {
        QString strFile  = _UTSOURCEDIR;
        strFile += "/test_sources/tar/compress/test.tar";
        KPluginMetaData data;
        QMimeDatabase db;
        QMimeType mimeFromContent = db.mimeTypeForFile(strFile, QMimeDatabase::MatchContent);
        const QVariantList args = {QVariant(strFile),
                                   QVariant().fromValue(data),
                                   QVariant::fromValue(mimeFromContent)
                                  };

        m_tester = new ReadWriteLibarchivePlugin(this, args);
    }

    virtual void TearDown()
    {
        delete m_tester;
    }

protected:
    ReadWriteLibarchivePlugin *m_tester;
};

TEST_F(UT_ReadWriteLibarchivePluginFactory, initTest)
{

}

TEST_F(UT_ReadWriteLibarchivePlugin, initTest)
{

}

TEST_F(UT_ReadWriteLibarchivePlugin, test_addFiles_001)
{
    QList<FileEntry> files;
    FileEntry file1;
    file1.strFullPath = _UTSOURCEDIR;
    file1.strFullPath += "/test_sources/tar/compress/test.txt";
    file1.qSize = 4;
    files.push_back(file1);
    CompressOptions options;
    QString strArchive = _UTSOURCEDIR;
    strArchive += "/test_sources/tar/compress/test.tar";
    QFile::remove(strArchive);
    EXPECT_EQ(m_tester->addFiles(files, options), PFT_Nomral);
}

TEST_F(UT_ReadWriteLibarchivePlugin, test_addFiles_002)
{
    QList<FileEntry> files;
    FileEntry file1;
    file1.strFullPath = _UTSOURCEDIR;
    file1.strFullPath += "/test_sources/tar/compress/dir";
    file1.isDirectory = true;
    file1.qSize = 4;
    files.push_back(file1);
    CompressOptions options;
    EXPECT_EQ(m_tester->addFiles(files, options), PFT_Nomral);
}

TEST_F(UT_ReadWriteLibarchivePlugin, test_deleteFiles_001)
{
    QList<FileEntry> files;
    FileEntry file1;
    file1.strFullPath = "test.txt";
    files.push_back(file1);
    EXPECT_EQ(m_tester->deleteFiles(files), PFT_Nomral);
}

TEST_F(UT_ReadWriteLibarchivePlugin, test_deleteFiles_002)
{
    QList<FileEntry> files;
    FileEntry file1;
    file1.strFullPath = "dir";
    file1.isDirectory = true;
    files.push_back(file1);
    EXPECT_EQ(m_tester->deleteFiles(files), PFT_Nomral);
}

bool open_stub(QIODevice::OpenMode flags)
{
    return false;
}

TEST_F(UT_ReadWriteLibarchivePlugin, test_initializeWriter)
{
    typedef bool (*fptr)(QIODevice::OpenMode);
    fptr QSaveFile_open = (fptr)(&QSaveFile::open);   //获取虚函数地址
    Stub stub;
    stub.set(QSaveFile_open, open_stub);

    EXPECT_EQ(m_tester->initializeWriter(), false);
}

int archive_filter_code_stub(struct archive *, int)
{
    return ARCHIVE_FILTER_NONE - 1;
}

TEST_F(UT_ReadWriteLibarchivePlugin, test_initializeWriterFilters)
{
    Stub stub;
    stub.set(archive_filter_code, archive_filter_code_stub);

    EXPECT_EQ(m_tester->initializeWriterFilters(), false);

    QString strPath = _UTSOURCEDIR;
    QFile::remove(strPath + "/test_sources/tar/compress/test.tar");
}