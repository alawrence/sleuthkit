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
 * \file TskFile.h
 * Contains the interface for the TskFile class.
 */

#ifndef _TSK_FILE_H
#define _TSK_FILE_H

// System includes
#include <string>

// Framework includes
#include "Services/TskImgDB.h"

/**
 * An interface that is used to represent a file. This interface
 * is used during the analysis of a file and is typically created
 * based on data in TskImgDB, which was created by CarveExtract
 * or TskImageFile.  Different implementations of this class 
 * may retrieve file content and metadata in different ways.
 * TskFile objects are obtained from TskFileManager.
 */
class TSK_FRAMEWORK_API TskFile
{
public:
	virtual ~TskFile();

    // Returns the file id.
    uint64_t id() const;

    // Get the type id
    int typeId() const;

    // Get the name
    std::string name() const;

    // Get the extension
    std::string extension() const;

    // Get the parent file id
    uint64_t parentFileId() const;

    // Get the directory type
    int dirType() const;

    // Get the metadata flags
    int metaType() const;

    // Get the directory flags
    int dirFlags() const;

    // Get the metadata flags
    int metaFlags() const;

    // Get the file size
    uint64_t size() const;

    // Get the change time
    int ctime() const;

    // Get the creation time
    int crtime() const;

    // Get the access time
    int atime() const;

    // Get the modify time
    int mtime() const;

    // Get the mode
    int mode() const;

    // Get the user id
    int uid() const;

    // Get the group id
    int gid() const;

    // Get the status
    int status() const;

    /// Fully qualified path to on-disk representation of file.
    virtual std::string getPath() const = 0;
    virtual void setPath(const std::string& path) = 0;

    // Get the file hash
    std::string getHash(TskImgDB::HASH_TYPE hashType) const;

    // Set the file hash
    void setHash(TskImgDB::HASH_TYPE hashType, const std::string hash);
    
    /// Does a file exist on disk for this TskFile object.
    /**
     * @return True if a file exists, false otherwise
     */ 
    virtual bool exists() const = 0;

    /// Does this file represent a directory.
    /**
     * @return True if this is a directory, false otherwise
     */ 
    virtual bool isDirectory() const = 0;

    /// Is this a "virtual" file 
    /**
     * @return True if this is a "virtual" file, false otherwise
     */ 
    virtual bool isVirtual() const = 0;

    /// Open the file. Must be called before reading.
    virtual void open() = 0;

    /// Close the file.
    virtual void close() = 0;

    /**
     * @param buf Buffer into which file content will be placed.
     * Must be at least "count" bytes in size.
     * @param count The number of bytes to read from the file.
     * @return The number of bytes read or 0 for end of file.
     */
    virtual ssize_t read(char * buf, const size_t count) = 0;

    // Read "count" bytes into "buf" starting at "offset".
    virtual ssize_t read(const int64_t offset, char * buf, const size_t count) = 0;

    /// Set the file status, also update the ImgDB file status
    virtual void setStatus(TskImgDB::FILE_STATUS status);

    virtual std::vector<TskBlackboardRecord> TskFile::getBlackboardRecords();

    std::string fullPath() const;
protected:
    // File id.
    uint64_t m_id;

    // Where the file is stored on disk
    std::string m_filePath;

    // Our current offset into the file
    uint64_t m_offset;

    // Is the file open (used for both on disk and image files)
    bool m_isOpen;

    // The database file record.
    TskFileRecord m_fileRecord;

    // Initialize the file from a database record
    void initialize();
};

#endif
