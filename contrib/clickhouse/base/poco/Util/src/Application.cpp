//
// Application.cpp
//
// Library: Util
// Package: Application
// Module:  Application
//
// Copyright (c) 2004-2006, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// SPDX-License-Identifier:	BSL-1.0
//


#include "DBPoco/Util/Application.h"
#include "DBPoco/Util/SystemConfiguration.h"
#include "DBPoco/Util/MapConfiguration.h"
#include "DBPoco/Util/PropertyFileConfiguration.h"
#include "DBPoco/Util/IniFileConfiguration.h"
#ifndef DB_POCO_UTIL_NO_XMLCONFIGURATION
#include "DBPoco/Util/XMLConfiguration.h"
#endif
#ifndef DB_POCO_UTIL_NO_JSONCONFIGURATION
#include "DBPoco/Util/JSONConfiguration.h"
#endif
#include "DBPoco/Util/LoggingSubsystem.h"
#include "DBPoco/Util/Option.h"
#include "DBPoco/Util/OptionProcessor.h"
#include "DBPoco/Util/Validator.h"
#include "DBPoco/Environment.h"
#include "DBPoco/Exception.h"
#include "DBPoco/NumberFormatter.h"
#include "DBPoco/File.h"
#include "DBPoco/Path.h"
#include "DBPoco/String.h"
#include "DBPoco/ConsoleChannel.h"
#include "DBPoco/AutoPtr.h"
#if defined(DB_POCO_OS_FAMILY_UNIX) && !defined(POCO_VXWORKS)
#include "DBPoco/SignalHandler.h"
#endif


using DBPoco::Logger;
using DBPoco::Path;
using DBPoco::File;
using DBPoco::Environment;
using DBPoco::SystemException;
using DBPoco::ConsoleChannel;
using DBPoco::NumberFormatter;
using DBPoco::AutoPtr;
using DBPoco::icompare;


namespace DBPoco {
namespace Util {


Application* Application::_pInstance = 0;


Application::Application():
	_pConfig(new LayeredConfiguration),
	_initialized(false),
	_unixOptions(true),
	_pLogger(&Logger::get("ApplicationStartup")),
	_stopOptionsProcessing(false)
{
	setup();
}


Application::Application(int argc, char* argv[]):
	_pConfig(new LayeredConfiguration),
	_initialized(false),
	_unixOptions(true),
	_pLogger(&Logger::get("ApplicationStartup")),
	_stopOptionsProcessing(false)
{
	setup();
	init(argc, argv);
}


Application::~Application()
{
	_pInstance = 0;
}


void Application::setup()
{
	DB_poco_assert (_pInstance == 0);
	
	_pConfig->add(new SystemConfiguration, PRIO_SYSTEM, false, false);
	_pConfig->add(new MapConfiguration, PRIO_APPLICATION, true, false);
	
	addSubsystem(new LoggingSubsystem);
	
#if defined(DB_POCO_OS_FAMILY_UNIX) && !defined(POCO_VXWORKS)
	_workingDirAtLaunch = Path::current();

	#if !defined(_DEBUG)
	DBPoco::SignalHandler::install();
	#endif
#else
	setUnixOptions(false);
#endif

	_pInstance = this;

	AutoPtr<ConsoleChannel> pCC = new ConsoleChannel;
	Logger::setChannel("", pCC);
}


void Application::addSubsystem(Subsystem* pSubsystem)
{
	DB_poco_check_ptr (pSubsystem);

	_subsystems.push_back(pSubsystem);
}


void Application::init(int argc, char* argv[])
{
	setArgs(argc, argv);
	init();
}




void Application::init(const ArgVec& args)
{
	setArgs(args);
	init();
}


void Application::init()
{
	Path appPath;
	getApplicationPath(appPath);
	_pConfig->setString("application.path", appPath.toString());
	_pConfig->setString("application.name", appPath.getFileName());
	_pConfig->setString("application.baseName", appPath.getBaseName());
	_pConfig->setString("application.dir", appPath.parent().toString());
	_pConfig->setString("application.configDir", Path::configHome() + appPath.getBaseName() + Path::separator());
	_pConfig->setString("application.cacheDir", Path::cacheHome() + appPath.getBaseName() + Path::separator());
	_pConfig->setString("application.tempDir", Path::tempHome() + appPath.getBaseName() + Path::separator());
	_pConfig->setString("application.dataDir", Path::dataHome() + appPath.getBaseName() + Path::separator());
	processOptions();
}


const char* Application::name() const
{
	return "Application";
}


void Application::initialize(Application& self)
{
	for (SubsystemVec::iterator it = _subsystems.begin(); it != _subsystems.end(); ++it)
	{
		_pLogger->debug(std::string("Initializing subsystem: ") + (*it)->name());
		(*it)->initialize(self);
	}
	_initialized = true;
}

	
void Application::uninitialize()
{
	if (_initialized)
	{
		for (SubsystemVec::reverse_iterator it = _subsystems.rbegin(); it != _subsystems.rend(); ++it)
		{
			_pLogger->debug(std::string("Uninitializing subsystem: ") + (*it)->name());
			(*it)->uninitialize();
		}
		_initialized = false;
	}
}


void Application::reinitialize(Application& self)
{
	for (SubsystemVec::iterator it = _subsystems.begin(); it != _subsystems.end(); ++it)
	{
		_pLogger->debug(std::string("Re-initializing subsystem: ") + (*it)->name());
		(*it)->reinitialize(self);
	}
}


void Application::setUnixOptions(bool flag)
{
	_unixOptions = flag;
}


int Application::loadConfiguration(int priority)
{
	int n = 0;
	Path appPath;
	getApplicationPath(appPath);
	Path confPath;
	if (findAppConfigFile(appPath.getBaseName(), "properties", confPath))
	{
		_pConfig->add(new PropertyFileConfiguration(confPath.toString()), priority, false, false);
		++n;
	}
#ifndef DB_POCO_UTIL_NO_INIFILECONFIGURATION
	if (findAppConfigFile(appPath.getBaseName(), "ini", confPath))
	{
		_pConfig->add(new IniFileConfiguration(confPath.toString()), priority, false, false);
		++n;
	}
#endif
#ifndef DB_POCO_UTIL_NO_JSONCONFIGURATION
	if (findAppConfigFile(appPath.getBaseName(), "json", confPath))
	{
		_pConfig->add(new JSONConfiguration(confPath.toString()), priority, false, false);
		++n;
	}
#endif
#ifndef DB_POCO_UTIL_NO_XMLCONFIGURATION
	if (findAppConfigFile(appPath.getBaseName(), "xml", confPath))
	{
		_pConfig->add(new XMLConfiguration(confPath.toString()), priority, false, false);
		++n;
	}
#endif
	if (n > 0)
	{
		if (!confPath.isAbsolute())
			_pConfig->setString("application.configDir", confPath.absolute().parent().toString());
		else
			_pConfig->setString("application.configDir", confPath.parent().toString());
	}
	return n;
}


void Application::loadConfiguration(const std::string& path, int priority)
{
	int n = 0;
	Path confPath(path);
	std::string ext = confPath.getExtension();
	if (icompare(ext, "properties") == 0)
	{
		_pConfig->add(new PropertyFileConfiguration(confPath.toString()), priority, false, false);
		++n;
	}
#ifndef DB_POCO_UTIL_NO_INIFILECONFIGURATION
	else if (icompare(ext, "ini") == 0)
	{
		_pConfig->add(new IniFileConfiguration(confPath.toString()), priority, false, false);
		++n;
	}
#endif
#ifndef DB_POCO_UTIL_NO_JSONCONFIGURATION
	else if (icompare(ext, "json") == 0)
	{
		_pConfig->add(new JSONConfiguration(confPath.toString()), priority, false, false);
		++n;
	}
#endif
#ifndef DB_POCO_UTIL_NO_XMLCONFIGURATION
	else if (icompare(ext, "xml") == 0)
	{
		_pConfig->add(new XMLConfiguration(confPath.toString()), priority, false, false);
		++n;
	}
#endif
	else throw DBPoco::InvalidArgumentException("Unsupported configuration file type", ext);

	if (n > 0 && !_pConfig->has("application.configDir"))
	{
		if (!confPath.isAbsolute())
			_pConfig->setString("application.configDir", confPath.absolute().parent().toString());
		else
			_pConfig->setString("application.configDir", confPath.parent().toString());
	}
}


std::string Application::commandName() const
{
	return _pConfig->getString("application.baseName");
}


std::string Application::commandPath() const
{
	return _pConfig->getString("application.path");
}


void Application::stopOptionsProcessing()
{
	_stopOptionsProcessing = true;
}


int Application::run()
{
	int rc = EXIT_CONFIG;
	initialize(*this);

	try
	{
		rc = EXIT_SOFTWARE;
		rc = main(_unprocessedArgs);
	}
	catch (DBPoco::Exception& exc)
	{
		logger().log(exc);
	}
	catch (std::exception& exc)
	{
		logger().error(exc.what());
	}
	catch (...)
	{
		logger().fatal("system exception");
	}

	uninitialize();
	return rc;
}


int Application::main(const ArgVec& args)
{
	return EXIT_OK;
}


void Application::setArgs(int argc, char* argv[])
{
	_command = argv[0];
	_pConfig->setInt("application.argc", argc);
	_unprocessedArgs.reserve(argc);
	std::string argvKey = "application.argv[";
	for (int i = 0; i < argc; ++i)
	{
		std::string arg(argv[i]);
		_pConfig->setString(argvKey + NumberFormatter::format(i) + "]", arg);
		_unprocessedArgs.push_back(arg);
	}
}


void Application::setArgs(const ArgVec& args)
{
	DB_poco_assert (!args.empty());
	
	_command = args[0];
	_pConfig->setInt("application.argc", (int) args.size());
	_unprocessedArgs = args;
	std::string argvKey = "application.argv[";
	for (int i = 0; i < args.size(); ++i)
	{
		_pConfig->setString(argvKey + NumberFormatter::format(i) + "]", args[i]);
	}
}


void Application::processOptions()
{
	defineOptions(_options);
	OptionProcessor processor(_options);
	processor.setUnixStyle(_unixOptions);
	_argv = _unprocessedArgs;
	_unprocessedArgs.erase(_unprocessedArgs.begin());
	ArgVec::iterator it = _unprocessedArgs.begin();
	while (it != _unprocessedArgs.end() && !_stopOptionsProcessing)
	{
		std::string name;
		std::string value;
		if (processor.process(*it, name, value))
		{
			if (!name.empty()) // "--" option to end options processing or deferred argument
			{
				handleOption(name, value);
			}
			it = _unprocessedArgs.erase(it);
		}
		else ++it;
	}
	if (!_stopOptionsProcessing)
		processor.checkRequired();
}


void Application::getApplicationPath(DBPoco::Path& appPath) const
{
#if defined(DB_POCO_OS_FAMILY_UNIX) && !defined(POCO_VXWORKS)
	if (_command.find('/') != std::string::npos)
	{
		Path path(_command);
		if (path.isAbsolute())
		{
			appPath = path;
		}
		else
		{
			appPath = _workingDirAtLaunch;
			appPath.append(path);
		}
	}
	else
	{
		if (!Path::find(Environment::get("PATH"), _command, appPath))
			appPath = Path(_workingDirAtLaunch, _command);
		appPath.makeAbsolute();
	}
#else
	appPath = _command;
#endif
}


bool Application::findFile(DBPoco::Path& path) const
{
	if (path.isAbsolute()) return true;
	
	Path appPath;
	getApplicationPath(appPath);
	Path base = appPath.parent();
	do
	{
		Path p(base, path);
		File f(p);
		if (f.exists())
		{
			path = p;
			return true;
		}
		if (base.depth() > 0) base.popDirectory();
	}
	while (base.depth() > 0);
	return false;
}


bool Application::findAppConfigFile(const std::string& appName, const std::string& extension, Path& path) const
{
	DB_poco_assert (!appName.empty());

	Path p(appName);
	p.setExtension(extension);
	bool found = findFile(p);
	if (!found)
	{
#if defined(_DEBUG)
		if (appName[appName.length() - 1] == 'd')
		{
			p.setBaseName(appName.substr(0, appName.length() - 1));
			found = findFile(p);
		}
#endif
	}
	if (found)
		path = p;
	return found;
}


bool Application::findAppConfigFile(const Path& basePath, const std::string& appName, const std::string& extension, Path& path) const
{
	DB_poco_assert (!appName.empty());
	
	Path p(basePath,appName);
	p.setExtension(extension);
	bool found = findFile(p);
	if (!found)
	{
#if defined(_DEBUG)
		if (appName[appName.length() - 1] == 'd')
		{
			p.setBaseName(appName.substr(0, appName.length() - 1));
			found = findFile(p);
		}
#endif
	}
	if (found)
		path = p;
	return found;
}


void Application::defineOptions(OptionSet& options)
{
	for (SubsystemVec::iterator it = _subsystems.begin(); it != _subsystems.end(); ++it)
	{
		(*it)->defineOptions(options);
	}
}


void Application::handleOption(const std::string& name, const std::string& value)
{
	const Option& option = _options.getOption(name);
	if (option.validator())
	{
		option.validator()->validate(option, value);
	}
	if (!option.binding().empty())
	{
		AbstractConfiguration* pConfig = option.config();
		if (!pConfig) pConfig = &config();
		pConfig->setString(option.binding(), value);
	}
	if (option.callback())
	{
		option.callback()->invoke(name, value);
	}
}


void Application::setLogger(Logger& logger)
{
	_pLogger = &logger;
}


} } // namespace DBPoco::Util
