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
 * \file TskPluginModule.cpp
 * Contains the implementation for the TskPluginModule class.
 */

// System includes
#include <sstream>

// Framework includes
#include "TskPluginModule.h"
#include "Services/TskServices.h"
#include "Utilities/TskException.h"
#include "Services/TskSystemPropertiesImpl.h"
#include "File/TskFileManagerImpl.h"

// Poco includes
#include "Poco/String.h"
#include "Poco/Path.h"

const std::string TskPluginModule::RUN_SYMBOL = "run";
const std::string TskPluginModule::REPORT_SYMBOL = "report";
const std::string TskPluginModule::INITIALIZE_SYMBOL = "initialize";
const std::string TskPluginModule::FINALIZE_SYMBOL = "finalize";

typedef TskModule::Status (*InitializeFunc)(std::string& args);
typedef TskModule::Status (*FinalizeFunc)();
typedef TskModule::Status (*RunFunc)(TskFile*);
typedef TskModule::Status (*ReportFunc)();

/**
 * Constructor
 */
TskPluginModule::TskPluginModule()
{
}

/**
 * Destructor
 */
TskPluginModule::~TskPluginModule()
{
    if (m_sharedLibrary.isLoaded())
    {
        // Call finalize function if defined
        if (m_sharedLibrary.hasSymbol(TskPluginModule::FINALIZE_SYMBOL))
        {
            FinalizeFunc fin = (FinalizeFunc)m_sharedLibrary.getSymbol(TskPluginModule::FINALIZE_SYMBOL);
            fin();
        }

        m_sharedLibrary.unload();
    }
}

/**
 * Load the module using path specified by location.
 * @param location Either a relative or fully qualified path to the dynamic library.
 */
void TskPluginModule::setPath(const std::string& location)
{
    try
    {
        // Call parent to search for location
        TskModule::setPath(location);

        // Confirm that the plugin module implements the required interface.
        m_sharedLibrary.load(m_modulePath);
    }
    catch (TskException& ex)
    {
        // Base class has already logged an error so we simply rethrow.
        throw ex;
    }
    catch(std::exception& ex)
    {
        // Log a message and throw a framework exception.
        std::wstringstream msg;
        msg << L"TskPluginModule::setPath - " << ex.what() << std::endl;
        LOGERROR(msg.str());

        throw TskException("Failed to set path: " + m_modulePath);
    }

}

/**
 * Runs the modules initialization function if it has one.
 * A non zero return code from the initialization function indicates
 * module initialization failure.
 */
void TskPluginModule::initialize()
{
    // Perform initialization if module implements the initialize function.
    if (m_sharedLibrary.hasSymbol(TskPluginModule::INITIALIZE_SYMBOL))
    {
        InitializeFunc init = (InitializeFunc) m_sharedLibrary.getSymbol(TskPluginModule::INITIALIZE_SYMBOL);
        std::string arguments = parameterSubstitution(m_arguments, 0);

        if (init(arguments) != TskModule::OK)
        {
            LOGERROR(L"TskPluginModule::Initialize - Module initialization failed.");

            throw TskException("Module initialization failed.");
        }
    }
}

bool TskPluginModule::isLoaded() const
{
    return (m_sharedLibrary.isLoaded());
}

void * TskPluginModule::getSymbol(const std::string symbol)
{
    return (void *)m_sharedLibrary.getSymbol(symbol);
}

bool TskPluginModule::hasSymbol(const std::string symbol) 
{
    return (m_sharedLibrary.hasSymbol(symbol));
}
