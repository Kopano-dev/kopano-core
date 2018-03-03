import imp
import glob
import os
import sys
import inspect

from plugintemplates import *


class PluginManager(object):
    def __init__(self, plugindir, logger):
        self.logger = logger
        self.plugindir = plugindir
        self.plugins = []

    def loadPlugins(self, baseclass):
        self.logger.logInfo("* Loading plugins started")
        if self.plugindir is None:
            raise Exception('Invalid plugins directory')

        directory = os.path.abspath(self.plugindir)
        if not os.path.isdir(directory):
            self.logger.logInfo(
                "! Plugins directory \"%s\" does not exist."
                "Plugins not loaded." % directory
            )
            return 0

        plugins = glob.glob(directory + "/*.py")

        self.logger.logInfo("** Checking plugins in %s" % directory)
        if directory not in sys.path:
            sys.path.insert(0, directory)

        for p in plugins:
            try:
                name = p.split("/")[-1]
                name = name.split(".py")[0]

                if name.startswith('_'):
                    continue

                self.logger.logDebug("** Inspecting file '%s.py'" % name)

                f, file, desc = imp.find_module(name, [directory])

                plugin = __import__(name)

                for k, v in inspect.getmembers(plugin):
                    if not inspect.isclass(v):
                        continue
                    if k.startswith('_'):
                        continue

                    if not issubclass(v, baseclass) or v is baseclass:
                        continue

                    instance = v(self.logger)
                    # fixme, use a plugin meta name ?
                    self.logger.logInfo("** Adding plugin '%s'" % k)
                    self.plugins.append((k, instance))

            except Exception as e:
                self.logger.logError("!-- failed to load: %s" % p)
                self.logger.logError("!-- error: %s " % e)

        self.logger.logInfo("* Loading plugins done")
        return len(self.plugins)

    def processPluginFunction(self, functionname, *args):
        plugins = []
        self.logger.logInfo("* %s processing started" % functionname)

        # get the priority of the plugin function
        for (name, i) in self.plugins:

            if hasattr(i, 'prio' + functionname):
                prio = getattr(i, 'prio' + functionname)
            else:
                prio = 9999

            plugins.append((functionname, prio, name, i))

        # sort the plugins on function name and prio
        sortedlist = sorted(plugins, key=lambda a: (a[0], a[1]))

        retval = MP_CONTINUE,
        for (fname, prio, cname, i) in sortedlist:
            if (fname != functionname):
                continue
            try:
                self.logger.logInfo("** Plugin '%s.%s' priority (%d)" % (
                    cname, fname, prio
                ))

                retval = getattr(i, fname)(*args)

                self.logger.logDebug("** Plugin '%s.%s' return result %s" % (
                    cname, fname, repr(retval)
                ))

                if (type(retval) == int or type(retval) == long):
                    self.logger.logDebug(
                        "!- Plugin '%s.%s' convert return value type "
                        "'%s' to a 'tuple'" % (cname, fname, type(retval))
                    )
                    retval = retval,
                if (type(retval) != tuple):
                    self.logger.logWarn(
                        "!- Plugin '%s.%s' returned a wrong"
                        "return value type '%s' except 'tuple'. "
                        "Fallback on defaults" % (cname, fname, type(retval))
                    )
                    retval = MP_CONTINUE,
                elif (type(retval[0]) != int and type(retval[0]) != long):
                    self.logger.logWarn(
                        "!- Plugin '%s.%s' returned a wrong return value "
                        "type '%s' expect 'int' or 'long' type. Fallback "
                        "on defaults" % (cname, fname, type(retval[0]))
                    )
                    retval = MP_CONTINUE,

                self.logger.logInfo(
                    "** Plugin '%s.%s' returncode %d" % (
                        cname, fname, retval[0]
                    )
                )
            except NotImplementedError as e:
                # Most likely the function is not implemented
                self.logger.logDebug("!---------- %s" % e)
            except Exception as e:
                self.logger.logError("!-- error: %s " % e)
                self.logger.logError(
                    "!- Plugin '%s.%s' call ignored please "
                    "check the plugin" % (cname, fname)
                )
                retval = MP_CONTINUE,

            if (retval[0] == MP_EXIT):
                break

        self.logger.logInfo("* %s processing done" % functionname)
        return retval
