# -*- coding: utf-8 -*-

"""
***************************************************************************
    GrassAlgorithmProvider.py
    ---------------------
    Date                 : August 2012
    Copyright            : (C) 2012 by Victor Olaya
    Email                : volayaf at gmail dot com
***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************
"""

__author__ = 'Victor Olaya'
__date__ = 'August 2012'
__copyright__ = '(C) 2012, Victor Olaya'

# This will get replaced with a git SHA1 when you do a git archive

__revision__ = '$Format:%H$'

import os

from PyQt4.QtGui import QIcon
from processing.core.ProcessingConfig import ProcessingConfig, Setting
from processing.core.AlgorithmProvider import AlgorithmProvider
from processing.core.ProcessingLog import ProcessingLog
from GrassUtils import GrassUtils
from GrassAlgorithm import GrassAlgorithm
from nviz import nviz
from processing.tools.system import isMac, isWindows

pluginPath = os.path.normpath(os.path.join(
    os.path.split(os.path.dirname(__file__))[0], os.pardir))


class GrassAlgorithmProvider(AlgorithmProvider):

    def __init__(self):
        AlgorithmProvider.__init__(self)
        self.activate = False
        self.createAlgsList()  # Preloading algorithms to speed up

    def initializeSettings(self):
        AlgorithmProvider.initializeSettings(self)
        if isWindows() or isMac():
            ProcessingConfig.addSetting(Setting(self.getDescription(),
                                                GrassUtils.GRASS_FOLDER, self.tr('GRASS folder'),
                                                GrassUtils.grassPath(), valuetype=Setting.FOLDER))
            ProcessingConfig.addSetting(Setting(self.getDescription(),
                                                GrassUtils.GRASS_WIN_SHELL, self.tr('Msys folder'),
                                                GrassUtils.grassWinShell(), valuetype=Setting.FOLDER))
        ProcessingConfig.addSetting(Setting(self.getDescription(),
                                            GrassUtils.GRASS_LOG_COMMANDS,
                                            self.tr('Log execution commands'), False))
        ProcessingConfig.addSetting(Setting(self.getDescription(),
                                            GrassUtils.GRASS_LOG_CONSOLE,
                                            self.tr('Log console output'), False))
        ProcessingConfig.addSetting(Setting(
            self.getDescription(),
            GrassUtils.GRASS_HELP_PATH,
            self.tr('Location of GRASS docs'),
            GrassUtils.grassHelpPath()))

    def unload(self):
        AlgorithmProvider.unload(self)
        if isWindows() or isMac():
            ProcessingConfig.removeSetting(GrassUtils.GRASS_FOLDER)
            ProcessingConfig.removeSetting(GrassUtils.GRASS_WIN_SHELL)
        ProcessingConfig.removeSetting(GrassUtils.GRASS_LOG_COMMANDS)
        ProcessingConfig.removeSetting(GrassUtils.GRASS_LOG_CONSOLE)
        ProcessingConfig.removeSetting(Grass7Utils.GRASS_HELP_PATH)

    def createAlgsList(self):
        self.preloadedAlgs = []
        folder = GrassUtils.grassDescriptionPath()
        for descriptionFile in os.listdir(folder):
            if descriptionFile.endswith('txt'):
                try:
                    alg = GrassAlgorithm(os.path.join(folder, descriptionFile))
                    if alg.name.strip() != '':
                        self.preloadedAlgs.append(alg)
                    else:
                        ProcessingLog.addToLog(ProcessingLog.LOG_ERROR,
                                               self.tr('Could not open GRASS algorithm: %s' % descriptionFile))
                except Exception as e:
                    ProcessingLog.addToLog(ProcessingLog.LOG_ERROR,
                                           self.tr('Could not open GRASS algorithm: %s' % descriptionFile))
        self.preloadedAlgs.append(nviz())

    def _loadAlgorithms(self):
        self.algs = self.preloadedAlgs

    def getDescription(self):
        return self.tr('GRASS commands')

    def getName(self):
        return 'grass'

    def getIcon(self):
        return QIcon(os.path.join(pluginPath, 'images', 'grass.svg'))

    def getSupportedOutputVectorLayerExtensions(self):
        return ['shp']

    def getSupportedOutputRasterLayerExtensions(self):
        return ['tif']

    def canBeActivated(self):
        return not bool(GrassUtils.checkGrassIsInstalled())
