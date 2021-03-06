/*
 * The Sleuth Kit
 *
 * Contact: Brian Carrier [carrier <at> sleuthkit [dot] org]
 * Copyright (c) 2010-2011 Basis Technology Corporation. All Rights
 * reserved.
 *
 * This software is distributed under the Common Public License 1.0
 */

/**
 * \file TskFileManagerImpl.cpp
 * Default implementation of the TskFileManagerImpl class.
 */

#include <sstream>

// Framework includes
#include "TskFileManagerImpl.h"
#include "TskFileTsk.h"
#include "Services/TskSystemPropertiesImpl.h"
#include "Services/TskServices.h"
#include "Utilities/TskException.h"
#include "Utilities/TskUtilities.h"

// Poco includes
#include "Poco/Exception.h"
#include "Poco/Path.h"
#include "Poco/FileStream.h"
#include "Poco/StreamCopier.h"

TskFileManagerImpl * TskFileManagerImpl::m_pInstance = NULL;

const int TskFileManagerImpl::FILES_PER_DIR = 1000;
const int TskFileManagerImpl::FILE_BUFFER_SIZE = 8192;
const std::string TskFileManagerImpl::FILES_DIRECTORY = "files";

TskFileManagerImpl& TskFileManagerImpl::instance()
{
    if (!m_pInstance)
    {
        m_pInstance = new TskFileManagerImpl();
        m_pInstance->initialize();
    }

    return *m_pInstance;
}

/**
 * Open a reference to our files folder, creating it if it does not
 * exist.
 */
void TskFileManagerImpl::initialize()
{
    try
    {
        std::string storagePath = TskUtilities::toUTF8(TSK_SYS_PROP_GET(TskSystemPropertiesImpl::OUT_DIR));
    
        m_storageDir = new Poco::File(storagePath + Poco::Path::separator() + FILES_DIRECTORY);

        // Create the directory if it does not exist.
        try {
            m_storageDir->createDirectory();
        } catch (Poco::FileExistsException &) {
            ; // Ignore. This can happen when another process is creating the same directory.
        }
    }
    catch (Poco::Exception& ex)
    {
        // Log a message
        std::wstringstream errorMsg;
        errorMsg << L"TskFileManagerImpl::initialize - File manager initialization failed with the following message: " 
            << ex.message().c_str() << std::endl;
        LOGERROR(errorMsg.str());
        
        // Throw a framework specific exception
        throw TskFileException(ex.message());
    }
}

/**
 * Create a TskFile object for the given id.
 */
TskFile * TskFileManagerImpl::getFile(const uint64_t fileId)
{
    // Check to see if a file named fileId exists.
    std::stringstream filePath;
    filePath << m_storageDir->path() << Poco::Path::separator()
        << fileId / FILES_PER_DIR << Poco::Path::separator() << fileId;

    Poco::File pocoFile(filePath.str());

    // call the relevant File constructor.
    if (pocoFile.exists())
        return new TskFileTsk(fileId, filePath.str());
    else
        return new TskFileTsk(fileId);
}

/**
 * Create a file object for the given fileId with on disk storage
 * located in the given path. The path must exist and must be a file.
 */
TskFile * TskFileManagerImpl::getFile(const uint64_t fileId, const std::wstring& path)
{
    Poco::File pocoFile(TskUtilities::toUTF8(path));

    if (pocoFile.exists() && pocoFile.isFile())
    {
        return new TskFileTsk(fileId, pocoFile.path());
    }
    else
    {
        throw new TskFileException("Failed to create file using " + pocoFile.path());
    }
}

/**
 * Returns the fully qualified path for the given fileId.
 * Will create the parent directory if it does not exist.
 */
std::wstring TskFileManagerImpl::getPath(const uint64_t fileId)
{
    // Determine which directory the file should live in.
    std::stringstream dirPath;
    dirPath << m_storageDir->path() << Poco::Path::separator()
        << fileId / FILES_PER_DIR;

    // Create the directory if it does not exist.
    Poco::File fileDir(dirPath.str());
    try {
        fileDir.createDirectory();
    } catch (Poco::FileExistsException &) {
        ; // Ignore. This can happen when another process is creating the same directory.
    }

    // Add the fileId onto the path
    dirPath << Poco::Path::separator() << fileId;

    // Convert from Poco's UTF8 representation to std::wstring
    std::wstring path = TskUtilities::toUTF16(dirPath.str());

    return path;
}

/**
 * Save the file to the given fully qualifed file path. The path includes the
 * name of the file. Directories along the path will be created if they do not exist.
 * If the destination file exists it will be replaced.
 */
void TskFileManagerImpl::saveFile(TskFile* fileToSave, const std::wstring& filePath)
{
    try 
    {
        if (fileToSave == NULL)
        {
            LOGERROR(L"TskFileManagerImpl::saveFile - Passed NULL file pointer.");
            throw TskNullPointerException();
        }

        Poco::Path destPath(TskUtilities::toUTF8(filePath));

        // Create directories that may be missing along the path.
        Poco::File destDir(destPath.parent());
        destDir.createDirectories();

        Poco::File destFile(destPath);

        // If the destination file exists it is replaced
        if (destFile.exists())
        {
            destFile.remove();
        }

        // If the source file exists we simply copy it to the target
        if (fileToSave->exists())
        {
            Poco::File sourceFile(fileToSave->getPath());
            sourceFile.copyTo(destPath.toString());
        }
        else
        {
            // We read the content from the file and write it to the target

            // Open the file whose content we are saving
            fileToSave->open();

            // Create a new empty file.
            destFile.createFile();

            // Call File.read() to get the file content and write to new file.
            Poco::FileOutputStream fos(destFile.path());
            char buffer[FILE_BUFFER_SIZE];
            int bytesRead = 0;

            do
            {
                memset(buffer, 0, FILE_BUFFER_SIZE);
                bytesRead = fileToSave->read(buffer, FILE_BUFFER_SIZE);
                if (bytesRead > 0)
                    fos.write(buffer, bytesRead);
            } while (bytesRead > 0);

            // Flush and close the output stream.
            fos.flush();
            fos.close();

            // Set the path for the newly saved file
            fileToSave->setPath(destFile.path());

            // Close the file
            fileToSave->close();
        }
    }
    catch (TskFileException& tskEx)
    {
        std::wstringstream errorMsg;
        errorMsg << L"TskFileManagerImpl::saveFile - " << tskEx.message().c_str() << std::endl;
        LOGERROR(errorMsg.str());

        // Rethrow the exception up to our caller
        throw;
    }
    catch (Poco::PathNotFoundException& ex)
    {
        std::wstringstream errorMsg;
        errorMsg << L"TskFileManagerImpl::saveFile - Path not found : "
            << ex.message().c_str() << std::endl;
        LOGERROR(errorMsg.str());

        throw TskFileException("Path not found : " + fileToSave->getPath());
    }
    catch (std::exception & ex)
    {
        std::wstringstream errorMsg;
        errorMsg << L"TskFileManagerImpl::saveFile - Exception : "
            << ex.what() << std::endl;
        LOGERROR(errorMsg.str());
        throw;
    }
}

/**
 * Save the file to it's default path
 */
void TskFileManagerImpl::saveFile(TskFile* fileToSave)
{
    // Determine what the path should be based on TskFile.id()
    // and call saveFile(fileToSave, path)
    saveFile(fileToSave, getPath(fileToSave->id()));
}

void TskFileManagerImpl::saveFile(const uint64_t fileId)
{
    // Use the default implementation in our parent class
    TskFileManager::saveFile(fileId);
}

void TskFileManagerImpl::saveFile(const uint64_t fileId, const std::wstring& filePath)
{
    // Use the default implementation in our parent class
    TskFileManager::saveFile(fileId, filePath);
}

/**
 * Save the file content represented by the input stream as the given file id.
 */
void TskFileManagerImpl::saveFile(const uint64_t fileId, std::istream& istr)
{
    // If a file with this id already exists we raise an error
    TskFile * pFile = getFile(fileId);

    if (pFile != NULL && pFile->exists())
    {
        std::stringstream msg;
        msg << "File id " << fileId << " already exists.";
        throw TskFileException(msg.str());
    }

    try
    {
        Poco::Path destPath(TskUtilities::toUTF8(getPath(fileId)));
        Poco::File destFile(destPath);

        // Create the destination
        destFile.createFile();

        // Save the file
        Poco::FileOutputStream fos(destFile.path(), std::ios::binary);
        Poco::StreamCopier::copyStream(istr, fos);
    }
    catch (Poco::Exception& ex)
    {
        std::wstringstream msg;
        msg << L"TskFileManagerImpl::saveFile - Error saving file from stream : " << ex.displayText().c_str();
        LOGERROR(msg.str());
        throw TskFileException("Error saving file from stream.");
    }
}

/**
 * Delete the given file.
 */
void TskFileManagerImpl::deleteFile(TskFile* fileToDelete)
{
    try
    {
        if (fileToDelete == NULL)
        {
            LOGERROR(L"TskFileManagerImpl::deleteFile - Passed NULL file pointer.");
            throw TskNullPointerException();
        }

        if (fileToDelete->exists())
        {
            Poco::File targetFile(fileToDelete->getPath());
            targetFile.remove();
            fileToDelete->setPath("");
        }
    }
    catch (Poco::Exception& ex)
    {
        std::wstringstream errorMsg;
        errorMsg << L"TskFileManagerImpl::delete - Failed to delete file " 
            << fileToDelete->getPath().c_str() << L". Error: " << ex.displayText().c_str() << std::endl;
        LOGERROR(errorMsg.str());

        throw TskFileException("Failed to delete file.");
    }
}

void TskFileManagerImpl::deleteFile(const uint64_t fileId)
{
    // Use the default implementation in our parent class
    TskFileManager::deleteFile(fileId);
}
