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
 * \file TskPipeline.cpp
 * Contains the implementation for the TskPipeline class.
 */

// System includes
#include <sstream>

// Framework includes
#include "TskPipeline.h"
#include "TskExecutableModule.h"
#include "TskPluginModule.h"
#include "File/TskFileManagerImpl.h"
#include "Services/TskServices.h"
#include "Utilities/TskException.h"
#include "Utilities/TskUtilities.h"

// Poco includes
#include "Poco/AutoPtr.h"
#include "Poco/NumberParser.h"
#include "Poco/DOM/DOMParser.h"
#include "Poco/DOM/Nodelist.h"
#include "Poco/DOM/Document.h"

const std::string TskPipeline::MODULE_ELEMENT = "MODULE";
const std::string TskPipeline::MODULE_TYPE_ATTR = "type";
const std::string TskPipeline::MODULE_ORDER_ATTR = "order";
const std::string TskPipeline::MODULE_LOCATION_ATTR = "location";
const std::string TskPipeline::MODULE_ARGS_ATTR = "arguments";
const std::string TskPipeline::MODULE_OUTPUT_ATTR = "output";
const std::string TskPipeline::MODULE_EXECUTABLE_TYPE = "executable";
const std::string TskPipeline::MODULE_PLUGIN_TYPE = "plugin";

TskPipeline::TskPipeline() : m_hasExeModule(false), m_loadDll(true)
{
}

TskPipeline::TskPipeline(TskPipeline& pipeline) : m_hasExeModule(false), m_loadDll(true)
{
    m_modules = pipeline.m_modules;
}

TskPipeline::~TskPipeline()
{
    // Delete modules
    for (std::vector<TskModule*>::iterator it = m_modules.begin(); it != m_modules.end(); it++)
        delete *it;
}

void TskPipeline::validate(const std::string & pipelineConfig)
{
    m_loadDll = false;
    initialize(pipelineConfig);
}

void TskPipeline::initialize(const std::string & pipelineConfig)
{
    if (pipelineConfig.empty())
    {
        LOGERROR(L"TskPipeline::initialize - Pipeline configuration string is empty.");
        throw TskException("Pipeline configuration string is empty.");
    }

    try
    {
        Poco::XML::DOMParser parser;
        Poco::AutoPtr<Poco::XML::Document> xmlDoc = parser.parseString(pipelineConfig);

        // Get all Module elements
        Poco::AutoPtr<Poco::XML::NodeList> modules = 
            xmlDoc->getElementsByTagName(TskPipeline::MODULE_ELEMENT);

        if (modules->length() == 0)
        {
            LOGWARN(L"TskPipeline::initialize - No modules found in config file.");
            return;
        }

        // Size our list based on the number of modules
        m_modules.resize(modules->length());

        // Iterate through the module elements, make sure the order starts with 1 with no gaps
        for (unsigned int i = 0; i < modules->length(); i++)
        {
            Poco::XML::Node * pNode = modules->item(i);
            Poco::XML::Element* pElem = dynamic_cast<Poco::XML::Element*>(pNode);
            Poco::XML::XMLString orderStr = pElem->getAttribute(TskPipeline::MODULE_ORDER_ATTR);
            if (orderStr == "") {
                LOGERROR(L"TskPipeline::initialize - Module order missing.");
                throw TskException("Module order missing.");
            }
            unsigned int order;
            try 
            {
                order = Poco::NumberParser::parse(orderStr);
            } catch (Poco::SyntaxException ex) 
            {
                std::wstringstream msg;
                msg << "TskPipeline::initialize - Module order must a decimal number. Got " << orderStr.c_str();
                LOGERROR(msg.str().c_str());
                throw TskException("Module order must a decimal number.");
            }
            if (order != i+1) 
            {
                std::wstringstream msg;
                msg << "TskPipeline::initialize - Expecting order " << i+1 << ", got " << order;
                LOGERROR(msg.str().c_str());
                throw TskException("Module order must start with 1 with no gaps.");
            }
        }

        // Iterate through the module elements creating a new Module for each one
        for (unsigned int i = 0; i < modules->length(); i++)
        {
            Poco::XML::Node * pNode = modules->item(i);
            Poco::XML::Element* pElem = dynamic_cast<Poco::XML::Element*>(pNode);

            if (!pElem)
                continue;

            // Create a new module
            TskModule * pModule = createModule(pElem);

            if (pModule == NULL)
            {
                LOGERROR(L"TskPipeline::initialize - Module creation failed.");
                throw TskException("Module creation failed.");
            }

            // Put the new module into the list if the slot isn't already taken.
            int order = Poco::NumberParser::parse(pElem->getAttribute(TskPipeline::MODULE_ORDER_ATTR));
            
            // Subtract 1 to reflect 0 based vector indexing.
            order--;

            if (order > m_modules.max_size())
            {
                std::wstringstream errorMsg;
                errorMsg << L"TskPipeline::initialize - Module order (" << order
                    << L") is greater than the number of modules (" << m_modules.max_size() << ")" ;
                LOGERROR(errorMsg.str());

                throw TskException("Module order greater than number of modules.");
            }

            if (m_modules[order] != NULL)
            {
                std::wstringstream errorMsg;
                errorMsg << L"TskPipeline::initialize - Position (" << order 
                    << L") is already occupied by a module." ;
                LOGERROR(errorMsg.str());

                throw TskException("Multiple modules with same order.");
            }

            m_modules[order] = pModule;

            if (m_loadDll) {
                TskImgDB& imgDB = TskServices::Instance().getImgDB();

                // Insert into Modules table
                int moduleId = 0;
                if (imgDB.addModule(m_modules[order]->getName(), "", moduleId)) {
                    std::wstringstream errorMsg;
                    errorMsg << L"TskPipeline::initialize - Failed to insert into Modules table. Module order=" << order 
                             << L" module name=" << TskUtilities::toUTF16(m_modules[order]->getName()) ;
                    LOGERROR(errorMsg.str());

                    throw TskException("Multiple modules with same order.");
                } else {
                    m_modules[order]->setModuleId(moduleId);
                }
            }
        }
    }
    catch (std::exception& ex)
    {
        std::wstringstream errorMsg;
        errorMsg << L"TskPipeline::initialize - Pipeline initialization failed: " <<ex.what() ;
        LOGERROR(errorMsg.str());

        throw TskException("Pipeline initialization failed.");
    }
}

void TskPipeline::run(const uint64_t fileId)
{
    if (m_modules.size() == 0)
        return;

    // Get a file object for the given fileId
    std::auto_ptr<TskFile> file(TskFileManagerImpl::instance().getFile(fileId));

    // Run the file object through the pipeline.
    run(file.get());
}

void TskPipeline::run(TskFile* file)
{
    if (m_modules.size() == 0)
        return;

    if (file == NULL)
    {
        LOGERROR(L"TskPipeline::run - Passed NULL file pointer.");
        throw TskNullPointerException();
    }

    TskImgDB& imgDB = TskServices::Instance().getImgDB();

    try
    {
        // If this is an excluded file or the file is not ready for analysis
        // we return without processing.
        if (excludeFile(file))
        {
            file->setStatus(TskImgDB::IMGDB_FILES_STATUS_ANALYSIS_SKIPPED);
            return;
        }

        if (file->status() != TskImgDB::IMGDB_FILES_STATUS_READY_FOR_ANALYSIS)
            return;

        // Update status to indicate analysis is in progress.
        file->setStatus(TskImgDB::IMGDB_FILES_STATUS_ANALYSIS_IN_PROGRESS);

        // If there is an Executable module in the pipeline we must
        // ensure that the file exists on disk.
        bool bCreated = false;

        if (!file->exists())
        {
            TskFileManagerImpl::instance().saveFile(file);
            bCreated = true;
        }

        for (int i = 0; i < m_modules.size(); i++)
        {
            TskModule::Status status = m_modules[i]->run(file);

            imgDB.setModuleStatus(file->id(), m_modules[i]->getModuleId(), (int)status);

            // Stop processing the file when a module tells us to.
            if (status == TskModule::STOP)
                break;
        }

        // Delete the file if we created it above.
        if (bCreated)
            TskFileManagerImpl::instance().deleteFile(file);

        // We allow modules to set status on the file so we only update it
        // if the modules haven't
        if (file->status() == TskImgDB::IMGDB_FILES_STATUS_ANALYSIS_IN_PROGRESS)
            file->setStatus(TskImgDB::IMGDB_FILES_STATUS_ANALYSIS_COMPLETE);
    }
    catch (std::exception& ex)
    {
        std::wstringstream msg;
        msg << L"TskPipeline::run - Error while processing file id (" << file->id()
            << L") : " << ex.what();
        LOGERROR(msg.str());
        imgDB.updateFileStatus(file->id(), TskImgDB::IMGDB_FILES_STATUS_ANALYSIS_FAILED);

        // Rethrow the exception
        throw;
    }
}

void TskPipeline::run()
{
    for (int i = 0; i < m_modules.size(); i++)
    {
        // Stop processing the file when a module tells us to.
        if (m_modules[i]->report() == TskModule::STOP)
            break;
    }
}

TskModule * TskPipeline::createModule(Poco::XML::Element *pElem)
{
    if (!pElem)
    {
        LOGERROR(L"TskPipeline::createModule - Passed NULL Element.");
        return NULL;
    }

    try
    {
        if (pElem->getAttribute(TskPipeline::MODULE_TYPE_ATTR) == MODULE_EXECUTABLE_TYPE)
        {
            // Use auto_ptr to ensure that module will be deleted if there 
            // are exceptions.
            std::auto_ptr<TskExecutableModule> pModule(new TskExecutableModule());
            pModule->setPath(pElem->getAttribute(TskPipeline::MODULE_LOCATION_ATTR));
            pModule->setArguments(pElem->getAttribute(TskPipeline::MODULE_ARGS_ATTR));
            pModule->setOutput(pElem->getAttribute(TskPipeline::MODULE_OUTPUT_ATTR));

            m_hasExeModule = true;

            // The module was successfully created so we no longer need the
            // auto_ptr to manage it.
            return pModule.release();
        }
        else if (pElem->getAttribute(TskPipeline::MODULE_TYPE_ATTR) == MODULE_PLUGIN_TYPE)
        {
            std::auto_ptr<TskPluginModule> pModule(createPluginModule());
            pModule->setPath(pElem->getAttribute(TskPipeline::MODULE_LOCATION_ATTR));
            pModule->setArguments(pElem->getAttribute(TskPipeline::MODULE_ARGS_ATTR));
            pModule->checkInterface();

            // Initialize the module. Will throw an exception on failure.
            if (m_loadDll)
                pModule->initialize();

            // The module was successfully created and initialized so we no longer
            // need the auto_ptr to manage it.
            return pModule.release();
        }
        else
        {
            std::wstringstream errorMsg;
            errorMsg << "TskPipeline::createModule - Unrecognized module type : "
                << pElem->getAttribute(TskPipeline::MODULE_TYPE_ATTR).c_str();
            LOGERROR(errorMsg.str());

            return NULL;
        }
    }
    catch (TskException& ex)
    {
        std::wstringstream errorMsg;
        errorMsg << L"TskPipeline::createModule - Module creation failed: " << ex.message().c_str();

        LOGERROR(errorMsg.str());
        return NULL;
    }
    catch (...)
    {
        LOGERROR(L"TskPipeline::createModule - Caught unknown exception.");
        return NULL;
    }

}

/**
 * Determine whether a particular file should be processed.
 * @returns true if file should be excluded, false otherwise
 */
bool TskPipeline::excludeFile(const TskFile* file)
{
    if (file == NULL)
    {
        LOGERROR(L"TskPipeline::excludeFile - Passed NULL file pointer.");
        throw TskNullPointerException();
    }

    // Exclude directories and Sleuthkit "virtual" files from analysis.
    if (file->isDirectory() || file->isVirtual())
        return true;

    return false;
}
