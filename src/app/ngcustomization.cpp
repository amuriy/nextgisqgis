/******************************************************************************
 * Project: NextGIS QGIS
 * Purpose: NextGIS QGIS Customization
 * Author:  Dmitry Baryshnikov, dmitry.baryshnikov@nextgis.com
 ******************************************************************************
 *   Copyright (c) 2017 NextGIS, <info@nextgis.com>
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Lesser General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/
#include "ngcustomization.h"
#include "ngqgsapplication.h"

#include "qgisappinterface.h"
#include "qgisappstylesheet.h"
#include "qgsadvanceddigitizingdockwidget.h"
#include "qgsapplayertreeviewmenuprovider.h"
#include "qgsattributeaction.h"
#include "qgsauthguiutils.h"
#include "qgsbrowserdockwidget.h"
#include "qgsclipboard.h"
#include "qgscredentialdialog.h"
#include "qgscomposer.h"
#include "qgscustomization.h"
#include "qgscustomlayerorderwidget.h"
#include "qgsdoublespinbox.h"
#include "qgsguivectorlayertools.h"
#include "qgslayertreelayer.h"
#include "qgslayertreemapcanvasbridge.h"
#include "qgslayertreemodel.h"
#include "qgslayertreeregistrybridge.h"
#include "qgslayertreeview.h"
#include "qgslayertreeviewdefaultactions.h"
#include "qgslayertreeutils.h"
#include "qgslegendfilterbutton.h"
#include "qgsmapcanvas.h"
#include "qgsmapcanvassnappingutils.h"
#include "qgsmapcanvastracer.h"
#include "qgsmaplayerregistry.h"
#include "qgsmapoverviewcanvas.h"
#include "qgsmaptoolpinlabels.h"
#include "qgsmaptoolmovelabel.h"
#include "qgsmaptoolshowhidelabels.h"
#include "qgsmaptoolrotatelabel.h"
#include "qgsmaptoolrotatepointsymbols.h"
#include "qgsmaplayeractionregistry.h"
#include "qgsmessagelogviewer.h"
#include "qgsmessageoutput.h"
#include "qgsmessageviewer.h"
#include "qgsnetworkaccessmanager.h"
#include "qgshandlebadlayers.h"
#include "qgsshortcutsmanager.h"
#include "qgsscalecombobox.h"
#include "qgspluginregistry.h"
#include "qgsproject.h"
#include "qgsproviderregistry.h"
#include "qgspythonrunner.h"
#include "qgspythonutilsimpl.h"
#include "qgsstatisticalsummarydockwidget.h"
#include "qgsstatusbarcoordinateswidget.h"
#include "qgsvisibilitypresets.h"
#include "qgsvectordataprovider.h"
#include "qgsundowidget.h"
#include "qgsuserinputdockwidget.h"
#include "qgstipgui.h"
#include "qgswelcomepage.h"
#include "qgsprojectproperties.h"
#include "qgsmeasuretool.h"
#include "qgsmaptoolmeasureangle.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QFileSystemWatcher>
#include <QMainWindow>
#include <QMessageBox>
#include <QProgressBar>
#include <QSplashScreen>
#include <QStackedWidget>
#include <QDesktopServices>
#include <QtXml>

// Editor widgets
#include "qgseditorwidgetregistry.h"

#include "ngsaboutdialog.h"

#ifdef Q_OS_MACX
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#endif // Q_OS_MACX

//------------------------------------------------------------------------------
// Implementation of the python runner
//------------------------------------------------------------------------------
class NGQgsPythonRunnerImpl : public QgsPythonRunner
{
  public:
    explicit NGQgsPythonRunnerImpl( QgsPythonUtils* pythonUtils ) : mPythonUtils( pythonUtils ) {}

    virtual bool runCommand( QString command, QString messageOnError = QString() ) override
    {
      if ( mPythonUtils && mPythonUtils->isEnabled() )
      {
        return mPythonUtils->runString( command, messageOnError, false );
      }
      return false;
    }

    virtual bool evalCommand( QString command, QString &result ) override
    {
      if ( mPythonUtils && mPythonUtils->isEnabled() )
      {
        return mPythonUtils->evalString( command, result );
      }
      return false;
    }

  protected:
    QgsPythonUtils* mPythonUtils;
};

//------------------------------------------------------------------------------
// NGQgisApp
//------------------------------------------------------------------------------
static bool ngCmpByText_( QAction* a, QAction* b )
{
  return QString::localeAwareCompare( a->text(), b->text() ) < 0;
}

static void ngSetTitleBarText_( QWidget & qgisApp )
{
    QString caption = QgisApp::tr( VENDOR " QGIS " ) +
            QLatin1String(VENDOR_VERSION);


  if ( QgsProject::instance()->title().isEmpty() )
  {
    if ( QgsProject::instance()->fileName().isEmpty() )
    {
      // no project title nor file name, so just leave caption with
      // application name and version
    }
    else
    {
      QFileInfo projectFileInfo( QgsProject::instance()->fileName() );
      caption += " - " + projectFileInfo.completeBaseName();
    }
  }
  else
  {
    caption += " - " + QgsProject::instance()->title();
  }

  qgisApp.setWindowTitle( caption );
}

static void ngCustomSrsValidation_( QgsCoordinateReferenceSystem &srs )
{
  NGQgisApp::instance()->emitCustomSrsValidation( srs );
}

static QgsMessageOutput *ngMessageOutputViewer_()
{
  if ( QThread::currentThread() == qApp->thread() )
    return new QgsMessageViewer( QgisApp::instance() );
  else
    return new QgsMessageOutputConsole();
}

NGQgisApp::NGQgisApp(QSplashScreen *splash, bool restorePlugins,
                     bool skipVersionCheck, QWidget *parent,
                     Qt::WindowFlags fl) : QgisApp( parent, fl )
{
    mSplash = splash;
    if ( smInstance )
    {
      QMessageBox::critical(
        this,
        tr( "Multiple Instances of QgisApp" ),
        tr( "Multiple instances of QGIS application object detected.\nPlease contact the developers.\n" ) );
      abort();
    }

    smInstance = this;

    setupNetworkAccessManager();

    // load GUI: actions, menus, toolbars
    setupUi( this );

    setupDatabase();

    setupAuthentication();

    // Create the themes folder for the user
    NGQgsApplication::createThemeFolder();

    mSplash->showMessage( tr( "Reading settings" ), Qt::AlignHCenter | Qt::AlignBottom );
    qApp->processEvents();

    mSplash->showMessage( tr( "Setting up the GUI" ), Qt::AlignHCenter | Qt::AlignBottom );
    qApp->processEvents();

    setupStyleSheet();

    setupCentralContainer(skipVersionCheck);

    createActions();
    createActionGroups();
    createMenus();
    createToolBars();
    createStatusBar();
    createCanvasTools();
    mMapCanvas->freeze();
    initLayerTreeView();
    createOverview();
    createMapTips();
    createDecorations();
    readSettings();
    updateRecentProjectPaths();
    updateProjectFromTemplates();
    legendLayerSelectionChanged();
    mSaveRollbackInProgress = false;

    QSettings settings;
    QFileSystemWatcher* projectsTemplateWatcher = new QFileSystemWatcher( this );
    QString templateDirName = settings.value( "/qgis/projectTemplateDir",
      NGQgsApplication::qgisSettingsDirPath() + "project_templates" ).toString();
    projectsTemplateWatcher->addPath( templateDirName );
    connect( projectsTemplateWatcher, SIGNAL( directoryChanged( QString ) ),
             this, SLOT( updateProjectFromTemplates() ) );

    // initialize the plugin manager
    mPluginManager = new QgsPluginManager( this, restorePlugins );

    addDockWidget( Qt::LeftDockWidgetArea, mUndoWidget );
    mUndoWidget->hide();

    mSnappingDialog = new QgsSnappingDialog( this, mMapCanvas );
    mSnappingDialog->setObjectName( "SnappingOption" );

    mBrowserWidget = new QgsBrowserDockWidget( tr( "Browser Panel" ), this );
    mBrowserWidget->setObjectName( "Browser" );
    addDockWidget( Qt::LeftDockWidgetArea, mBrowserWidget );
    mBrowserWidget->hide();

    mBrowserWidget2 = new QgsBrowserDockWidget( tr( "Browser Panel (2)" ), this );
    mBrowserWidget2->setObjectName( "Browser2" );
    addDockWidget( Qt::LeftDockWidgetArea, mBrowserWidget2 );
    mBrowserWidget2->hide();

    addDockWidget( Qt::LeftDockWidgetArea, mAdvancedDigitizingDockWidget );
    mAdvancedDigitizingDockWidget->hide();

    addDockWidget( Qt::LeftDockWidgetArea, mStatisticalSummaryDockWidget );
    mStatisticalSummaryDockWidget->hide();

    addDockWidget( Qt::LeftDockWidgetArea, mBookMarksDockWidget );
    mBookMarksDockWidget->hide();

    QMainWindow::addDockWidget( Qt::BottomDockWidgetArea, mUserInputDockWidget );
    mUserInputDockWidget->setFloating( true );

    mLastMapToolMessage = nullptr;

    mLogViewer = new QgsMessageLogViewer( statusBar(), this );

    mLogDock = new QDockWidget( tr( "Log Messages Panel" ), this );
    mLogDock->setObjectName( "MessageLog" );
    mLogDock->setAllowedAreas( Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea );
    addDockWidget( Qt::BottomDockWidgetArea, mLogDock );
    mLogDock->setWidget( mLogViewer );
    mLogDock->hide();
    connect( mMessageButton, SIGNAL( toggled( bool ) ), mLogDock,
             SLOT( setVisible( bool ) ) );
    connect( mLogDock, SIGNAL( visibilityChanged( bool ) ), mMessageButton,
             SLOT( setChecked( bool ) ) );
    connect( QgsMessageLog::instance(), SIGNAL( messageReceived( bool ) ), this,
             SLOT( toggleLogMessageIcon( bool ) ) );
    connect( mMessageButton, SIGNAL( toggled( bool ) ), this,
             SLOT( toggleLogMessageIcon( bool ) ) );
    mVectorLayerTools = new QgsGuiVectorLayerTools();

    // Init the editor widget types
    QgsEditorWidgetRegistry::initEditors( mMapCanvas, mInfoBar );

    mInternalClipboard = new QgsClipboard; // create clipboard
    connect( mInternalClipboard, SIGNAL( changed() ), this,
             SLOT( clipboardChanged() ) );
    mQgisInterface = new QgisAppInterface( this ); // create the interface

  #ifdef Q_OS_MAC
    // action for Window menu (create before generating WindowTitleChange event))
    mWindowAction = new QAction( this );
    connect( mWindowAction, SIGNAL( triggered() ), this, SLOT( activate() ) );

    // add this window to Window menu
    addWindow( mWindowAction );
  #endif

    activateDeactivateLayerRelatedActions( nullptr ); // after members were created

    connect( QgsMapLayerActionRegistry::instance(), SIGNAL( changed() ), this,
             SLOT( refreshActionFeatureAction() ) );

    // set application's caption
    QString caption = QgisApp::tr( VENDOR " QGIS " ) +
            QLatin1String(VENDOR_VERSION);
    setWindowTitle( caption );

    QgsMessageLog::logMessage( tr( "QGIS starting..." ), QString::null,
                               QgsMessageLog::INFO );

    // set QGIS specific srs validation
    connect( this, SIGNAL( customSrsValidation( QgsCoordinateReferenceSystem& ) ),
             this, SLOT( validateSrs( QgsCoordinateReferenceSystem& ) ) );
    QgsCoordinateReferenceSystem::setCustomSrsValidation( ngCustomSrsValidation_ );

    // set graphical message output
    QgsMessageOutput::setMessageOutputCreator( ngMessageOutputViewer_ );

    // set graphical credential requester
    new QgsCredentialDialog( this );

    qApp->processEvents();

    // load providers
    mSplash->showMessage( tr( "Checking provider plugins" ), Qt::AlignHCenter |
                          Qt::AlignBottom );
    qApp->processEvents();
    NGQgsApplication::initQgis();

    mSplash->showMessage( tr( "Starting Python" ), Qt::AlignHCenter |
                          Qt::AlignBottom );
    qApp->processEvents();
    loadPythonSupport();

    // Create the plugin registry and load plugins
    // load any plugins that were running in the last session
    mSplash->showMessage( tr( "Restoring loaded plugins" ), Qt::AlignHCenter |
                          Qt::AlignBottom );
    qApp->processEvents();
    QgsPluginRegistry::instance()->setQgisInterface( mQgisInterface );
    if ( restorePlugins )
    {
      // Restoring of plugins can be disabled with --noplugins command line option
      // because some plugins may cause QGIS to crash during startup
      QgsPluginRegistry::instance()->restoreSessionPlugins(
                  NGQgsApplication::pluginPath() );

      // Also restore plugins from user specified plugin directories
      QString myPaths = settings.value( "plugins/searchPathsForPlugins", "" ).toString();
      if ( !myPaths.isEmpty() )
      {
        QStringList myPathList = myPaths.split( '|' );
        QgsPluginRegistry::instance()->restoreSessionPlugins( myPathList );
      }
    }

    if ( mPythonUtils && mPythonUtils->isEnabled() )
    {
      // initialize the plugin installer to start fetching repositories in background
      QgsPythonRunner::run( "import pyplugin_installer" );
      QgsPythonRunner::run( "pyplugin_installer.initPluginInstaller()" );
      // enable Python in the Plugin Manager and pass the PythonUtils to it
      mPluginManager->setPythonUtils( mPythonUtils );
    }
    else if ( mActionShowPythonDialog )
    {
      // python is disabled so get rid of the action for python console
      delete mActionShowPythonDialog;
      mActionShowPythonDialog = nullptr;
    }

    // Set icon size of toolbars
    int size = settings.value( "/IconSize", QGIS_ICON_SIZE ).toInt();
    setIconSizes( size );

    mSplash->showMessage( tr( "Initializing file filters" ), Qt::AlignHCenter |
                          Qt::AlignBottom );
    qApp->processEvents();

    // now build vector and raster file filters
    mVectorFileFilter = QgsProviderRegistry::instance()->fileVectorFilters();
    mRasterFileFilter = QgsProviderRegistry::instance()->fileRasterFilters();

    QgsProject::instance()->setBadLayerHandler( new QgsHandleBadLayersHandler() );

    setupConnections();

    mSplash->showMessage( tr( "Restoring window state" ), Qt::AlignHCenter |
                          Qt::AlignBottom );
    qApp->processEvents();
    restoreWindowState();

    QgsCustomization::instance()->updateMainWindow( mToolbarMenu );

    mSplash->showMessage( tr( "QGIS Ready!" ), Qt::AlignHCenter |
                          Qt::AlignBottom );

    QgsMessageLog::logMessage( NGQgsApplication::showSettings(), QString::null,
                               QgsMessageLog::INFO );

    QgsMessageLog::logMessage( tr( "QGIS Ready!" ), QString::null,
                               QgsMessageLog::INFO );

    mMapTipsVisible = false;
    mTrustedMacros = false;

    // setup drag drop
    setAcceptDrops( true );

    mFullScreenMode = false;
    mPrevScreenModeMaximized = false;
    show();
    qApp->processEvents();

    mMapCanvas->freeze( false );
    mMapCanvas->clearExtentHistory(); // reset zoomnext/zoomlast
    mLastComposerId = 0;


    // Show a nice tip of the day
    if ( settings.value( QString( "/qgis/showTips%1" ).arg( VERSION_INT / 100 ),
                         true ).toBool() )
    {
      mSplash->hide();
      QDir::setCurrent(NGQgsApplication::iconsPath());
      QgsTipGui myTip( this );
      myTip.exec();
    }
    else
    {
      QgsDebugMsg( "Tips are disabled" );
    }


    // supposedly all actions have been added, now register them to the shortcut manager
    QgsShortcutsManager::instance()->registerAllChildrenActions( this );

    QgsProviderRegistry::instance()->registerGuis( this );

    // update windows
    qApp->processEvents();

    // NOTE: Don't annoy user ( void )QgsAuthGuiUtils::isDisabled( messageBar() );

    fileNewBlank();

    NGQgsApplication::setFileOpenEventReceiver( this );

    const QString updaterPath = updateProgrammPath();
    if(!QFile::exists(updaterPath))
    {
      mActionCheckQgisVersion->setEnabled(false);
      // mActionCheckQgisVersion->setToolTip(tr("There is not ") + this->updateProgrammName);
    }
}

NGQgisApp::~NGQgisApp()
{
}

void NGQgisApp::setupNetworkAccessManager()
{
  QgsNetworkAccessManager *nam = QgsNetworkAccessManager::instance();

  connect( nam, SIGNAL( authenticationRequired( QNetworkReply *,
                                                QAuthenticator * ) ),
           this, SLOT( namAuthenticationRequired( QNetworkReply *,
                                                  QAuthenticator * ) ) );

  connect( nam, SIGNAL( proxyAuthenticationRequired( const QNetworkProxy &,
                                                     QAuthenticator * ) ),
           this, SLOT( namProxyAuthenticationRequired( const QNetworkProxy &,
                                                       QAuthenticator * ) ) );

  connect( nam, SIGNAL( requestTimedOut( QNetworkReply* ) ),
           this, SLOT( namRequestTimedOut( QNetworkReply* ) ) );

#ifndef QT_NO_OPENSSL
  connect( nam, SIGNAL( sslErrors( QNetworkReply *, const QList<QSslError> & ) ),
           this, SLOT( namSslErrors( QNetworkReply *, const QList<QSslError> & ) ) );
#endif
}

void NGQgisApp::setupDatabase()
{
    mSplash->showMessage( tr( "Checking database" ), Qt::AlignHCenter |
                          Qt::AlignBottom );
    qApp->processEvents();
    // Do this early on before anyone else opens it and prevents us copying it
    QString dbError;
    if ( !NGQgsApplication::createDB( &dbError ) )
    {
      QMessageBox::critical( this, tr( "Private qgis.db" ), dbError );
    }
}

void NGQgisApp::setupAuthentication()
{
    mSplash->showMessage( tr( "Initializing authentication" ), Qt::AlignHCenter |
                          Qt::AlignBottom );
    qApp->processEvents();
    QgsAuthManager::instance()->init( NGQgsApplication::pluginPath() );
    if ( !QgsAuthManager::instance()->isDisabled() )
    {
        connect( QgsAuthManager::instance(), SIGNAL( messageOut( const QString&,
                                const QString&, QgsAuthManager::MessageLevel ) ),
                 this, SLOT( authMessageOut( const QString&, const QString&,
                                             QgsAuthManager::MessageLevel ) ) );
        connect( QgsAuthManager::instance(), SIGNAL( authDatabaseEraseRequested() ),
                 this, SLOT( eraseAuthenticationDatabase() ) );
    }
}

void NGQgisApp::setupStyleSheet()
{
    // set up stylesheet builder and apply saved or default style options
    mStyleSheetBuilder = new QgisAppStyleSheet( this );
    connect( mStyleSheetBuilder, SIGNAL( appStyleSheetChanged( const QString& ) ),
             this, SLOT( setAppStyleSheet( const QString& ) ) );
    mStyleSheetBuilder->buildStyleSheet( mStyleSheetBuilder->defaultOptions() );
}

void NGQgisApp::setupCentralContainer(bool skipVersionCheck)
{
    QSettings settings;
    QWidget *centralWidget = this->centralWidget();
    QGridLayout *centralLayout = new QGridLayout( centralWidget );
    centralWidget->setLayout( centralLayout );
    centralLayout->setContentsMargins( 0, 0, 0, 0 );

    // "theMapCanvas" used to find this canonical instance later
    mMapCanvas = new QgsMapCanvas( centralWidget, "theMapCanvas" );
    connect( mMapCanvas, SIGNAL( messageEmitted( const QString&,
                                const QString&, QgsMessageBar::MessageLevel ) ),
             this, SLOT( displayMessage( const QString&, const QString&,
                                         QgsMessageBar::MessageLevel ) ) );
    mMapCanvas->setWhatsThis( tr( "Map canvas. This is where raster and vector "
                                  "layers are displayed when added to the map" ) );

    // set canvas color right away
    int myRed = settings.value( "/qgis/default_canvas_color_red", 255 ).toInt();
    int myGreen = settings.value( "/qgis/default_canvas_color_green", 255 ).toInt();
    int myBlue = settings.value( "/qgis/default_canvas_color_blue", 255 ).toInt();
    mMapCanvas->setCanvasColor( QColor( myRed, myGreen, myBlue ) );

    // what type of project to auto-open
    mProjOpen = settings.value( "/qgis/projOpenAtLaunch", 0 ).toInt();

    mWelcomePage = new QgsWelcomePage( skipVersionCheck );

    mCentralContainer = new QStackedWidget;
    mCentralContainer->insertWidget( 0, mMapCanvas );
    mCentralContainer->insertWidget( 1, mWelcomePage );

    centralLayout->addWidget( mCentralContainer, 0, 0, 2, 1 );

    connect( mMapCanvas, SIGNAL( layersChanged() ), this, SLOT( showMapCanvas() ) );

    mCentralContainer->setCurrentIndex( mProjOpen ? 0 : 1 );

    // a bar to warn the user with non-blocking messages
    mInfoBar = new QgsMessageBar( centralWidget );
    mInfoBar->setSizePolicy( QSizePolicy::Minimum, QSizePolicy::Fixed );
    centralLayout->addWidget( mInfoBar, 0, 0, 1, 1 );

    // User Input Dock Widget
    mUserInputDockWidget = new QgsUserInputDockWidget( this );
    mUserInputDockWidget->setObjectName( "UserInputDockWidget" );

    //set the focus to the map canvas
    mMapCanvas->setFocus();

    mLayerTreeView = new QgsLayerTreeView( this );
    mLayerTreeView->setObjectName( "theLayerTreeView" );

    // create undo widget
    mUndoWidget = new QgsUndoWidget( nullptr, mMapCanvas );
    mUndoWidget->setObjectName( "Undo" );

    // Advanced Digitizing dock
    mAdvancedDigitizingDockWidget = new QgsAdvancedDigitizingDockWidget(
                mMapCanvas, this );
    mAdvancedDigitizingDockWidget->setObjectName( "AdvancedDigitizingTools" );

    // Statistical Summary dock
    mStatisticalSummaryDockWidget = new QgsStatisticalSummaryDockWidget( this );
    mStatisticalSummaryDockWidget->setObjectName( "StatistalSummaryDockWidget" );

    // Bookmarks dock
    mBookMarksDockWidget = new QgsBookmarks( this );
    mBookMarksDockWidget->setObjectName( "BookmarksDockWidget" );

    mSnappingUtils = new QgsMapCanvasSnappingUtils( mMapCanvas, this );
    mMapCanvas->setSnappingUtils( mSnappingUtils );
    connect( QgsProject::instance(), SIGNAL( snapSettingsChanged() ),
             mSnappingUtils, SLOT( readConfigFromProject() ) );
    connect( this, SIGNAL( projectRead() ), mSnappingUtils,
             SLOT( readConfigFromProject() ) );
}

void NGQgisApp::createActions()
{
  mActionPluginSeparator1 = nullptr;  // plugin list separator will be created when the first plugin is loaded
  mActionPluginSeparator2 = nullptr;  // python separator will be created only if python is found
  mActionRasterSeparator = nullptr;   // raster plugins list separator will be created when the first plugin is loaded

  // Project Menu Items
  connect( mActionNewProject, SIGNAL( triggered() ), this, SLOT( fileNew() ) );
  connect( mActionNewBlankProject, SIGNAL( triggered() ), this, SLOT( fileNewBlank() ) );
  connect( mActionOpenProject, SIGNAL( triggered() ), this, SLOT( fileOpen() ) );
  connect( mActionSaveProject, SIGNAL( triggered() ), this, SLOT( fileSave() ) );
  connect( mActionSaveProjectAs, SIGNAL( triggered() ), this, SLOT( fileSaveAs() ) );
  connect( mActionSaveMapAsImage, SIGNAL( triggered() ), this, SLOT( saveMapAsImage() ) );
  connect( mActionNewPrintComposer, SIGNAL( triggered() ), this, SLOT( newPrintComposer() ) );
  connect( mActionShowComposerManager, SIGNAL( triggered() ), this, SLOT( showComposerManager() ) );
  connect( mActionExit, SIGNAL( triggered() ), this, SLOT( fileExit() ) );
  connect( mActionDxfExport, SIGNAL( triggered() ), this, SLOT( dxfExport() ) );

  // Edit Menu Items
  connect( mActionUndo, SIGNAL( triggered() ), mUndoWidget, SLOT( undo() ) );
  connect( mActionRedo, SIGNAL( triggered() ), mUndoWidget, SLOT( redo() ) );
  connect( mActionCutFeatures, SIGNAL( triggered() ), this, SLOT( editCut() ) );
  connect( mActionCopyFeatures, SIGNAL( triggered() ), this, SLOT( editCopy() ) );
  connect( mActionPasteFeatures, SIGNAL( triggered() ), this, SLOT( editPaste() ) );
  connect( mActionPasteAsNewVector, SIGNAL( triggered() ), this, SLOT( pasteAsNewVector() ) );
  connect( mActionPasteAsNewMemoryVector, SIGNAL( triggered() ), this, SLOT( pasteAsNewMemoryVector() ) );
  connect( mActionCopyStyle, SIGNAL( triggered() ), this, SLOT( copyStyle() ) );
  connect( mActionPasteStyle, SIGNAL( triggered() ), this, SLOT( pasteStyle() ) );
  connect( mActionAddFeature, SIGNAL( triggered() ), this, SLOT( addFeature() ) );
  connect( mActionCircularStringCurvePoint, SIGNAL( triggered() ), this, SLOT( circularStringCurvePoint() ) );
  connect( mActionCircularStringRadius, SIGNAL( triggered() ), this, SLOT( circularStringRadius() ) );
  connect( mActionMoveFeature, SIGNAL( triggered() ), this, SLOT( moveFeature() ) );
  connect( mActionRotateFeature, SIGNAL( triggered() ), this, SLOT( rotateFeature() ) );

  connect( mActionReshapeFeatures, SIGNAL( triggered() ), this, SLOT( reshapeFeatures() ) );
  connect( mActionSplitFeatures, SIGNAL( triggered() ), this, SLOT( splitFeatures() ) );
  connect( mActionSplitParts, SIGNAL( triggered() ), this, SLOT( splitParts() ) );
  connect( mActionDeleteSelected, SIGNAL( triggered() ), this, SLOT( deleteSelected() ) );
  connect( mActionAddRing, SIGNAL( triggered() ), this, SLOT( addRing() ) );
  connect( mActionFillRing, SIGNAL( triggered() ), this, SLOT( fillRing() ) );
  connect( mActionAddPart, SIGNAL( triggered() ), this, SLOT( addPart() ) );
  connect( mActionSimplifyFeature, SIGNAL( triggered() ), this, SLOT( simplifyFeature() ) );
  connect( mActionDeleteRing, SIGNAL( triggered() ), this, SLOT( deleteRing() ) );
  connect( mActionDeletePart, SIGNAL( triggered() ), this, SLOT( deletePart() ) );
  connect( mActionMergeFeatures, SIGNAL( triggered() ), this, SLOT( mergeSelectedFeatures() ) );
  connect( mActionMergeFeatureAttributes, SIGNAL( triggered() ), this, SLOT( mergeAttributesOfSelectedFeatures() ) );
  connect( mActionNodeTool, SIGNAL( triggered() ), this, SLOT( nodeTool() ) );
  connect( mActionRotatePointSymbols, SIGNAL( triggered() ), this, SLOT( rotatePointSymbols() ) );
  connect( mActionSnappingOptions, SIGNAL( triggered() ), this, SLOT( snappingOptions() ) );
  connect( mActionOffsetCurve, SIGNAL( triggered() ), this, SLOT( offsetCurve() ) );

  // View Menu Items
  connect( mActionPan, SIGNAL( triggered() ), this, SLOT( pan() ) );
  connect( mActionPanToSelected, SIGNAL( triggered() ), this, SLOT( panToSelected() ) );
  connect( mActionZoomIn, SIGNAL( triggered() ), this, SLOT( zoomIn() ) );
  connect( mActionZoomOut, SIGNAL( triggered() ), this, SLOT( zoomOut() ) );
  connect( mActionSelectFeatures, SIGNAL( triggered() ), this, SLOT( selectFeatures() ) );
  connect( mActionSelectPolygon, SIGNAL( triggered() ), this, SLOT( selectByPolygon() ) );
  connect( mActionSelectFreehand, SIGNAL( triggered() ), this, SLOT( selectByFreehand() ) );
  connect( mActionSelectRadius, SIGNAL( triggered() ), this, SLOT( selectByRadius() ) );
  connect( mActionDeselectAll, SIGNAL( triggered() ), this, SLOT( deselectAll() ) );
  connect( mActionSelectAll, SIGNAL( triggered() ), this, SLOT( selectAll() ) );
  connect( mActionInvertSelection, SIGNAL( triggered() ), this, SLOT( invertSelection() ) );
  connect( mActionSelectByExpression, SIGNAL( triggered() ), this, SLOT( selectByExpression() ) );
  connect( mActionIdentify, SIGNAL( triggered() ), this, SLOT( identify() ) );
  connect( mActionFeatureAction, SIGNAL( triggered() ), this, SLOT( doFeatureAction() ) );
  connect( mActionMeasure, SIGNAL( triggered() ), this, SLOT( measure() ) );
  connect( mActionMeasureArea, SIGNAL( triggered() ), this, SLOT( measureArea() ) );
  connect( mActionMeasureAngle, SIGNAL( triggered() ), this, SLOT( measureAngle() ) );
  connect( mActionZoomFullExtent, SIGNAL( triggered() ), this, SLOT( zoomFull() ) );
  connect( mActionZoomToLayer, SIGNAL( triggered() ), this, SLOT( zoomToLayerExtent() ) );
  connect( mActionZoomToSelected, SIGNAL( triggered() ), this, SLOT( zoomToSelected() ) );
  connect( mActionZoomLast, SIGNAL( triggered() ), this, SLOT( zoomToPrevious() ) );
  connect( mActionZoomNext, SIGNAL( triggered() ), this, SLOT( zoomToNext() ) );
  connect( mActionZoomActualSize, SIGNAL( triggered() ), this, SLOT( zoomActualSize() ) );
  connect( mActionMapTips, SIGNAL( triggered() ), this, SLOT( toggleMapTips() ) );
  connect( mActionNewBookmark, SIGNAL( triggered() ), this, SLOT( newBookmark() ) );
  connect( mActionShowBookmarks, SIGNAL( triggered() ), this, SLOT( showBookmarks() ) );
  connect( mActionDraw, SIGNAL( triggered() ), this, SLOT( refreshMapCanvas() ) );
  connect( mActionTextAnnotation, SIGNAL( triggered() ), this, SLOT( addTextAnnotation() ) );
  connect( mActionFormAnnotation, SIGNAL( triggered() ), this, SLOT( addFormAnnotation() ) );
  connect( mActionHtmlAnnotation, SIGNAL( triggered() ), this, SLOT( addHtmlAnnotation() ) );
  connect( mActionSvgAnnotation, SIGNAL( triggered() ), this, SLOT( addSvgAnnotation() ) );
  connect( mActionAnnotation, SIGNAL( triggered() ), this, SLOT( modifyAnnotation() ) );
  connect( mActionLabeling, SIGNAL( triggered() ), this, SLOT( labeling() ) );
  connect( mActionStatisticalSummary, SIGNAL( triggered() ), this, SLOT( showStatisticsDockWidget() ) );

  // Layer Menu Items
  connect( mActionNewVectorLayer, SIGNAL( triggered() ), this, SLOT( newVectorLayer() ) );
  connect( mActionNewSpatiaLiteLayer, SIGNAL( triggered() ), this, SLOT( newSpatialiteLayer() ) );
  connect( mActionNewMemoryLayer, SIGNAL( triggered() ), this, SLOT( newMemoryLayer() ) );
  connect( mActionShowRasterCalculator, SIGNAL( triggered() ), this, SLOT( showRasterCalculator() ) );
  connect( mActionShowAlignRasterTool, SIGNAL( triggered() ), this, SLOT( showAlignRasterTool() ) );
  connect( mActionEmbedLayers, SIGNAL( triggered() ), this, SLOT( embedLayers() ) );
  connect( mActionAddLayerDefinition, SIGNAL( triggered() ), this, SLOT( addLayerDefinition() ) );
  connect( mActionAddOgrLayer, SIGNAL( triggered() ), this, SLOT( addVectorLayer() ) );
  connect( mActionAddRasterLayer, SIGNAL( triggered() ), this, SLOT( addRasterLayer() ) );
  connect( mActionAddPgLayer, SIGNAL( triggered() ), this, SLOT( addDatabaseLayer() ) );
  connect( mActionAddSpatiaLiteLayer, SIGNAL( triggered() ), this, SLOT( addSpatiaLiteLayer() ) );
  connect( mActionAddMssqlLayer, SIGNAL( triggered() ), this, SLOT( addMssqlLayer() ) );
  connect( mActionAddOracleLayer, SIGNAL( triggered() ), this, SLOT( addOracleLayer() ) );
  connect( mActionAddWmsLayer, SIGNAL( triggered() ), this, SLOT( addWmsLayer() ) );
  connect( mActionAddWcsLayer, SIGNAL( triggered() ), this, SLOT( addWcsLayer() ) );
  connect( mActionAddWfsLayer, SIGNAL( triggered() ), this, SLOT( addWfsLayer() ) );
  connect( mActionAddDelimitedText, SIGNAL( triggered() ), this, SLOT( addDelimitedTextLayer() ) );
  connect( mActionAddVirtualLayer, SIGNAL( triggered() ), this, SLOT( addVirtualLayer() ) );
  connect( mActionOpenTable, SIGNAL( triggered() ), this, SLOT( attributeTable() ) );
  connect( mActionOpenFieldCalc, SIGNAL( triggered() ), this, SLOT( fieldCalculator() ) );
  connect( mActionToggleEditing, SIGNAL( triggered() ), this, SLOT( toggleEditing() ) );
  connect( mActionSaveLayerEdits, SIGNAL( triggered() ), this, SLOT( saveActiveLayerEdits() ) );
  connect( mActionSaveEdits, SIGNAL( triggered() ), this, SLOT( saveEdits() ) );
  connect( mActionSaveAllEdits, SIGNAL( triggered() ), this, SLOT( saveAllEdits() ) );
  connect( mActionRollbackEdits, SIGNAL( triggered() ), this, SLOT( rollbackEdits() ) );
  connect( mActionRollbackAllEdits, SIGNAL( triggered() ), this, SLOT( rollbackAllEdits() ) );
  connect( mActionCancelEdits, SIGNAL( triggered() ), this, SLOT( cancelEdits() ) );
  connect( mActionCancelAllEdits, SIGNAL( triggered() ), this, SLOT( cancelAllEdits() ) );
  connect( mActionLayerSaveAs, SIGNAL( triggered() ), this, SLOT( saveAsFile() ) );
  connect( mActionSaveLayerDefinition, SIGNAL( triggered() ), this, SLOT( saveAsLayerDefinition() ) );
  connect( mActionRemoveLayer, SIGNAL( triggered() ), this, SLOT( removeLayer() ) );
  connect( mActionDuplicateLayer, SIGNAL( triggered() ), this, SLOT( duplicateLayers() ) );
  connect( mActionSetLayerScaleVisibility, SIGNAL( triggered() ), this, SLOT( setLayerScaleVisibility() ) );
  connect( mActionSetLayerCRS, SIGNAL( triggered() ), this, SLOT( setLayerCRS() ) );
  connect( mActionSetProjectCRSFromLayer, SIGNAL( triggered() ), this, SLOT( setProjectCRSFromLayer() ) );
  connect( mActionLayerProperties, SIGNAL( triggered() ), this, SLOT( layerProperties() ) );
  connect( mActionLayerSubsetString, SIGNAL( triggered() ), this, SLOT( layerSubsetString() ) );
  connect( mActionAddToOverview, SIGNAL( triggered() ), this, SLOT( isInOverview() ) );
  connect( mActionAddAllToOverview, SIGNAL( triggered() ), this, SLOT( addAllToOverview() ) );
  connect( mActionRemoveAllFromOverview, SIGNAL( triggered() ), this, SLOT( removeAllFromOverview() ) );
  connect( mActionShowAllLayers, SIGNAL( triggered() ), this, SLOT( showAllLayers() ) );
  connect( mActionHideAllLayers, SIGNAL( triggered() ), this, SLOT( hideAllLayers() ) );
  connect( mActionShowSelectedLayers, SIGNAL( triggered() ), this, SLOT( showSelectedLayers() ) );
  connect( mActionHideSelectedLayers, SIGNAL( triggered() ), this, SLOT( hideSelectedLayers() ) );

  // Plugin Menu Items
  connect( mActionManagePlugins, SIGNAL( triggered() ), this, SLOT( showPluginManager() ) );
  connect( mActionShowPythonDialog, SIGNAL( triggered() ), this, SLOT( showPythonDialog() ) );

  // Settings Menu Items
  connect( mActionToggleFullScreen, SIGNAL( triggered() ), this, SLOT( toggleFullScreen() ) );
  connect( mActionProjectProperties, SIGNAL( triggered() ), this, SLOT( projectProperties() ) );
  connect( mActionOptions, SIGNAL( triggered() ), this, SLOT( options() ) );
  connect( mActionCustomProjection, SIGNAL( triggered() ), this, SLOT( customProjection() ) );
  connect( mActionConfigureShortcuts, SIGNAL( triggered() ), this, SLOT( configureShortcuts() ) );
  connect( mActionStyleManagerV2, SIGNAL( triggered() ), this, SLOT( showStyleManagerV2() ) );
//  connect( mActionCustomization, SIGNAL( triggered() ), this, SLOT( customize() ) );

#ifdef Q_OS_MAC
  // Window Menu Items
  mActionWindowMinimize = new QAction( tr( "Minimize" ), this );
  mActionWindowMinimize->setShortcut( tr( "Ctrl+M", "Minimize Window" ) );
  mActionWindowMinimize->setStatusTip( tr( "Minimizes the active window to the dock" ) );
  connect( mActionWindowMinimize, SIGNAL( triggered() ), this, SLOT( showActiveWindowMinimized() ) );

  mActionWindowZoom = new QAction( tr( "Zoom" ), this );
  mActionWindowZoom->setStatusTip( tr( "Toggles between a predefined size and the window size set by the user" ) );
  connect( mActionWindowZoom, SIGNAL( triggered() ), this, SLOT( toggleActiveWindowMaximized() ) );

  mActionWindowAllToFront = new QAction( tr( "Bring All to Front" ), this );
  mActionWindowAllToFront->setStatusTip( tr( "Bring forward all open windows" ) );
  connect( mActionWindowAllToFront, SIGNAL( triggered() ), this, SLOT( bringAllToFront() ) );

  // list of open windows
  mWindowActions = new QActionGroup( this );
#endif

  // Vector edits menu
  QMenu* menuAllEdits = new QMenu( tr( "Current Edits" ), this );
  menuAllEdits->addAction( mActionSaveEdits );
  menuAllEdits->addAction( mActionRollbackEdits );
  menuAllEdits->addAction( mActionCancelEdits );
  menuAllEdits->addSeparator();
  menuAllEdits->addAction( mActionSaveAllEdits );
  menuAllEdits->addAction( mActionRollbackAllEdits );
  menuAllEdits->addAction( mActionCancelAllEdits );
  mActionAllEdits->setMenu( menuAllEdits );

  // Raster toolbar items
  connect( mActionLocalHistogramStretch, SIGNAL( triggered() ), this, SLOT( localHistogramStretch() ) );
  connect( mActionFullHistogramStretch, SIGNAL( triggered() ), this, SLOT( fullHistogramStretch() ) );
  connect( mActionLocalCumulativeCutStretch, SIGNAL( triggered() ), this, SLOT( localCumulativeCutStretch() ) );
  connect( mActionFullCumulativeCutStretch, SIGNAL( triggered() ), this, SLOT( fullCumulativeCutStretch() ) );
  connect( mActionIncreaseBrightness, SIGNAL( triggered() ), this, SLOT( increaseBrightness() ) );
  connect( mActionDecreaseBrightness, SIGNAL( triggered() ), this, SLOT( decreaseBrightness() ) );
  connect( mActionIncreaseContrast, SIGNAL( triggered() ), this, SLOT( increaseContrast() ) );
  connect( mActionDecreaseContrast, SIGNAL( triggered() ), this, SLOT( decreaseContrast() ) );

  // Vector Menu Items
//  connect( mActionOSMDownload, SIGNAL( triggered() ), this, SLOT( osmDownloadDialog() ) );
//  connect( mActionOSMImport, SIGNAL( triggered() ), this, SLOT( osmImportDialog() ) );
//  connect( mActionOSMExport, SIGNAL( triggered() ), this, SLOT( osmExportDialog() ) );

  // Help Menu Items
#ifdef Q_OS_MAC
  mActionHelpContents->setShortcut( QString( "Ctrl+?" ) );
  mActionQgisHomePage->setShortcut( QString() );
  mActionReportaBug->setShortcut( QString() );
#endif

// TODO: path to pdf
//  mActionHelpContents->setEnabled( QFileInfo( NGQgsApplication::pkgDataPath() + "/doc/NextGISQGIS.pdf" ).exists() );

  connect( mActionHelpContents, SIGNAL( triggered() ), this, SLOT( helpContents() ) );
//  connect( mActionHelpAPI, SIGNAL( triggered() ), this, SLOT( apiDocumentation() ) );
  connect( mActionReportaBug, SIGNAL( triggered() ), this, SLOT( reportaBug() ) );
  connect( mActionNeedSupport, SIGNAL( triggered() ), this, SLOT( supportProviders() ) );
  connect( mActionQgisHomePage, SIGNAL( triggered() ), this, SLOT( helpQgisHomePage() ) );
  connect( mActionCheckQgisVersion, SIGNAL( triggered() ), this, SLOT( checkQgisVersion() ) );
  connect( mActionAbout, SIGNAL( triggered() ), this, SLOT( about() ) );
//  connect( mActionSponsors, SIGNAL( triggered() ), this, SLOT( sponsors() ) );

  connect( mActionShowPinnedLabels, SIGNAL( toggled( bool ) ), this, SLOT( showPinnedLabels( bool ) ) );
  connect( mActionPinLabels, SIGNAL( triggered() ), this, SLOT( pinLabels() ) );
  connect( mActionShowHideLabels, SIGNAL( triggered() ), this, SLOT( showHideLabels() ) );
  connect( mActionMoveLabel, SIGNAL( triggered() ), this, SLOT( moveLabel() ) );
  connect( mActionRotateLabel, SIGNAL( triggered() ), this, SLOT( rotateLabel() ) );
  connect( mActionChangeLabelProperties, SIGNAL( triggered() ), this, SLOT( changeLabelProperties() ) );

#ifndef HAVE_POSTGRESQL
  delete mActionAddPgLayer;
  mActionAddPgLayer = 0;
#endif

#ifndef HAVE_MSSQL
  delete mActionAddMssqlLayer;
  mActionAddMssqlLayer = 0;
#endif

#ifndef HAVE_ORACLE
  delete mActionAddOracleLayer;
  mActionAddOracleLayer = nullptr;
#endif

}

void NGQgisApp::createActionGroups()
{
  mMapToolGroup = new QActionGroup( this );
  mMapToolGroup->addAction( mActionPan );
  mMapToolGroup->addAction( mActionZoomIn );
  mMapToolGroup->addAction( mActionZoomOut );
  mMapToolGroup->addAction( mActionIdentify );
  mMapToolGroup->addAction( mActionFeatureAction );
  mMapToolGroup->addAction( mActionSelectFeatures );
  mMapToolGroup->addAction( mActionSelectPolygon );
  mMapToolGroup->addAction( mActionSelectFreehand );
  mMapToolGroup->addAction( mActionSelectRadius );
  mMapToolGroup->addAction( mActionDeselectAll );
  mMapToolGroup->addAction( mActionSelectAll );
  mMapToolGroup->addAction( mActionInvertSelection );
  mMapToolGroup->addAction( mActionMeasure );
  mMapToolGroup->addAction( mActionMeasureArea );
  mMapToolGroup->addAction( mActionMeasureAngle );
  mMapToolGroup->addAction( mActionAddFeature );
  mMapToolGroup->addAction( mActionCircularStringCurvePoint );
  mMapToolGroup->addAction( mActionCircularStringRadius );
  mMapToolGroup->addAction( mActionMoveFeature );
  mMapToolGroup->addAction( mActionRotateFeature );
#if defined(GEOS_VERSION_MAJOR) && defined(GEOS_VERSION_MINOR) && \
    ((GEOS_VERSION_MAJOR>3) || ((GEOS_VERSION_MAJOR==3) && (GEOS_VERSION_MINOR>=3)))
  mMapToolGroup->addAction( mActionOffsetCurve );
#endif
  mMapToolGroup->addAction( mActionReshapeFeatures );
  mMapToolGroup->addAction( mActionSplitFeatures );
  mMapToolGroup->addAction( mActionSplitParts );
  mMapToolGroup->addAction( mActionDeleteSelected );
  mMapToolGroup->addAction( mActionAddRing );
  mMapToolGroup->addAction( mActionFillRing );
  mMapToolGroup->addAction( mActionAddPart );
  mMapToolGroup->addAction( mActionSimplifyFeature );
  mMapToolGroup->addAction( mActionDeleteRing );
  mMapToolGroup->addAction( mActionDeletePart );
  mMapToolGroup->addAction( mActionMergeFeatures );
  mMapToolGroup->addAction( mActionMergeFeatureAttributes );
  mMapToolGroup->addAction( mActionNodeTool );
  mMapToolGroup->addAction( mActionRotatePointSymbols );
  mMapToolGroup->addAction( mActionPinLabels );
  mMapToolGroup->addAction( mActionShowHideLabels );
  mMapToolGroup->addAction( mActionMoveLabel );
  mMapToolGroup->addAction( mActionRotateLabel );
  mMapToolGroup->addAction( mActionChangeLabelProperties );

  // Preview Modes Group
  QActionGroup* mPreviewGroup = new QActionGroup( this );
  mPreviewGroup->setExclusive( true );
  mActionPreviewModeOff->setActionGroup( mPreviewGroup );
  mActionPreviewModeGrayscale->setActionGroup( mPreviewGroup );
  mActionPreviewModeMono->setActionGroup( mPreviewGroup );
  mActionPreviewProtanope->setActionGroup( mPreviewGroup );
  mActionPreviewDeuteranope->setActionGroup( mPreviewGroup );
}

void NGQgisApp::createMenus()
{
  // Panel and Toolbar Submenus
  mPanelMenu = new QMenu( tr( "Panels" ), this );
  mPanelMenu->setObjectName( "mPanelMenu" );
  mToolbarMenu = new QMenu( tr( "Toolbars" ), this );
  mToolbarMenu->setObjectName( "mToolbarMenu" );

  // Get platform for menu layout customization (Gnome, Kde, Mac, Win)
  QDialogButtonBox::ButtonLayout layout =
    QDialogButtonBox::ButtonLayout( style()->styleHint(
                             QStyle::SH_DialogButtonLayout, nullptr, this ) );

  // Project Menu

  // Connect once for the entire submenu.
  connect( mRecentProjectsMenu, SIGNAL( triggered( QAction * ) ),
           this, SLOT( openProject( QAction * ) ) );
  connect( mProjectFromTemplateMenu, SIGNAL( triggered( QAction * ) ),
           this, SLOT( fileNewFromTemplateAction( QAction * ) ) );

  if ( layout == QDialogButtonBox::GnomeLayout ||
       layout == QDialogButtonBox::MacLayout ||
       layout == QDialogButtonBox::WinLayout )
  {
    QAction* before = mActionNewPrintComposer;
    mSettingsMenu->removeAction( mActionProjectProperties );
    mProjectMenu->insertAction( before, mActionProjectProperties );
    mProjectMenu->insertSeparator( before );
  }

  // View Menu

  if ( layout != QDialogButtonBox::KdeLayout )
  {
    mViewMenu->addSeparator();
    mViewMenu->addMenu( mPanelMenu );
    mViewMenu->addMenu( mToolbarMenu );
    mViewMenu->addAction( mActionToggleFullScreen );
  }
  else
  {
    // on the top of the settings menu
    QAction* before = mActionProjectProperties;
    mSettingsMenu->insertMenu( before, mPanelMenu );
    mSettingsMenu->insertMenu( before, mToolbarMenu );
    mSettingsMenu->insertAction( before, mActionToggleFullScreen );
    mSettingsMenu->insertSeparator( before );
  }


#ifdef Q_OS_MAC
  mMenuPreviewMode->menuAction()->setVisible( false );

  mProjectMenu->addAction( mActionAbout );
  mProjectMenu->addAction( mActionOptions );

  // Window Menu

  mWindowMenu = new QMenu( tr( "Window" ), this );

  mWindowMenu->addAction( mActionWindowMinimize );
  mWindowMenu->addAction( mActionWindowZoom );
  mWindowMenu->addSeparator();

  mWindowMenu->addAction( mActionWindowAllToFront );
  mWindowMenu->addSeparator();

  // insert before Help menu, as per Mac OS convention
  menuBar()->insertMenu( mHelpMenu->menuAction(), mWindowMenu );
#endif

  // Database Menu
  // don't add it yet, wait for a plugin
  mDatabaseMenu = new QMenu( tr( "&Database" ), menuBar() );
  mDatabaseMenu->setObjectName( "mDatabaseMenu" );
  // Web Menu
  // don't add it yet, wait for a plugin
  mWebMenu = new QMenu( tr( "&Web" ), menuBar() );
  mWebMenu->setObjectName( "mWebMenu" );
}

void NGQgisApp::createToolBars()
{
  QSettings settings;
  // QSize myIconSize ( 32,32 ); //large icons
  // Note: we need to set each object name to ensure that
  // qmainwindow::saveState and qmainwindow::restoreState
  // work properly

  QList<QToolBar*> toolbarMenuToolBars;
  toolbarMenuToolBars << mFileToolBar
  << mLayerToolBar
  << mDigitizeToolBar
  << mAdvancedDigitizeToolBar
  << mMapNavToolBar
  << mAttributesToolBar
  << mPluginToolBar
 // << mHelpToolBar
  << mRasterToolBar
  << mVectorToolBar
  << mDatabaseToolBar
  << mWebToolBar
  << mLabelToolBar;

  delete mHelpToolBar;
  mHelpToolBar = nullptr;

  QList<QAction*> toolbarMenuActions;
  // Set action names so that they can be used in customization
  Q_FOREACH ( QToolBar *toolBar, toolbarMenuToolBars )
  {
    toolBar->toggleViewAction()->setObjectName( "mActionToggle" +
                                                toolBar->objectName().mid( 1 ) );
    toolbarMenuActions << toolBar->toggleViewAction();
  }

  // sort actions in toolbar menu
  qSort( toolbarMenuActions.begin(), toolbarMenuActions.end(), ngCmpByText_ );

  mToolbarMenu->addActions( toolbarMenuActions );

  // selection tool button

  QToolButton *bt = new QToolButton( mAttributesToolBar );
  bt->setPopupMode( QToolButton::MenuButtonPopup );
  QList<QAction*> selectActions;
  selectActions << mActionSelectByExpression << mActionSelectAll
  << mActionInvertSelection;
  bt->addActions( selectActions );
  bt->setDefaultAction( mActionSelectByExpression );
  QAction* selectionAction = mAttributesToolBar->insertWidget( mActionDeselectAll, bt );
  selectionAction->setObjectName( "ActionSelection" );

  // select tool button
  bt = new QToolButton( mAttributesToolBar );
  bt->setPopupMode( QToolButton::MenuButtonPopup );
  QList<QAction*> selectionActions;
  selectionActions << mActionSelectFeatures << mActionSelectPolygon
  << mActionSelectFreehand << mActionSelectRadius;
  bt->addActions( selectionActions );

  QAction* defSelectAction = mActionSelectFeatures;
  switch ( settings.value( "/UI/selectTool", 0 ).toInt() )
  {
    case 0:
      defSelectAction = mActionSelectFeatures;
      break;
    case 1:
      defSelectAction = mActionSelectFeatures;
      break;
    case 2:
      defSelectAction = mActionSelectRadius;
      break;
    case 3:
      defSelectAction = mActionSelectPolygon;
      break;
    case 4:
      defSelectAction = mActionSelectFreehand;
      break;
  }
  bt->setDefaultAction( defSelectAction );
  QAction* selectAction = mAttributesToolBar->insertWidget( selectionAction, bt );
  selectAction->setObjectName( "ActionSelect" );
  connect( bt, SIGNAL( triggered( QAction * ) ), this,
           SLOT( toolButtonActionTriggered( QAction * ) ) );

  // feature action tool button

  bt = new QToolButton( mAttributesToolBar );
  bt->setPopupMode( QToolButton::MenuButtonPopup );
  bt->setDefaultAction( mActionFeatureAction );
  mFeatureActionMenu = new QMenu( bt );
  connect( mFeatureActionMenu, SIGNAL( triggered( QAction * ) ), this,
           SLOT( updateDefaultFeatureAction( QAction * ) ) );
  connect( mFeatureActionMenu, SIGNAL( aboutToShow() ), this,
           SLOT( refreshFeatureActions() ) );
  bt->setMenu( mFeatureActionMenu );
  QAction* featureActionAction = mAttributesToolBar->insertWidget( selectAction, bt );
  featureActionAction->setObjectName( "ActionFeatureAction" );

  // measure tool button
  bt = new QToolButton( mAttributesToolBar );
  bt->setPopupMode( QToolButton::MenuButtonPopup );
  bt->addAction( mActionMeasure );
  bt->addAction( mActionMeasureArea );
  bt->addAction( mActionMeasureAngle );

  QAction* defMeasureAction = mActionMeasure;
  switch ( settings.value( "/UI/measureTool", 0 ).toInt() )
  {
    case 0:
      defMeasureAction = mActionMeasure;
      break;
    case 1:
      defMeasureAction = mActionMeasureArea;
      break;
    case 2:
      defMeasureAction = mActionMeasureAngle;
      break;
  }
  bt->setDefaultAction( defMeasureAction );
  QAction* measureAction = mAttributesToolBar->insertWidget( mActionMapTips, bt );
  measureAction->setObjectName( "ActionMeasure" );
  connect( bt, SIGNAL( triggered( QAction * ) ), this,
           SLOT( toolButtonActionTriggered( QAction * ) ) );

  // annotation tool button
  bt = new QToolButton();
  bt->setPopupMode( QToolButton::MenuButtonPopup );
  bt->addAction( mActionTextAnnotation );
  bt->addAction( mActionFormAnnotation );
  bt->addAction( mActionHtmlAnnotation );
  bt->addAction( mActionSvgAnnotation );
  bt->addAction( mActionAnnotation );

  QAction* defAnnotationAction = mActionTextAnnotation;
  switch ( settings.value( "/UI/annotationTool", 0 ).toInt() )
  {
    case 0:
      defAnnotationAction = mActionTextAnnotation;
      break;
    case 1:
      defAnnotationAction = mActionFormAnnotation;
      break;
    case 2:
      defAnnotationAction = mActionHtmlAnnotation;
      break;
    case 3:
      defAnnotationAction = mActionSvgAnnotation;
      break;
    case 4:
      defAnnotationAction =  mActionAnnotation;
      break;

  }
  bt->setDefaultAction( defAnnotationAction );
  QAction* annotationAction = mAttributesToolBar->addWidget( bt );
  annotationAction->setObjectName( "ActionAnnotation" );
  connect( bt, SIGNAL( triggered( QAction * ) ), this,
           SLOT( toolButtonActionTriggered( QAction * ) ) );

  // vector layer edits tool buttons
  QToolButton* tbAllEdits = qobject_cast<QToolButton *>(
              mDigitizeToolBar->widgetForAction( mActionAllEdits ) );
  tbAllEdits->setPopupMode( QToolButton::InstantPopup );

  // new layer tool button
  bt = new QToolButton();
  bt->setPopupMode( QToolButton::MenuButtonPopup );
  bt->addAction( mActionNewVectorLayer );
  bt->addAction( mActionNewSpatiaLiteLayer );
  bt->addAction( mActionNewMemoryLayer );

  QAction* defNewLayerAction = mActionNewVectorLayer;
  switch ( settings.value( "/UI/defaultNewLayer", 1 ).toInt() )
  {
    case 0:
      defNewLayerAction = mActionNewSpatiaLiteLayer;
      break;
    case 1:
      defNewLayerAction = mActionNewVectorLayer;
      break;
    case 2:
      defNewLayerAction = mActionNewMemoryLayer;
      break;
  }
  bt->setDefaultAction( defNewLayerAction );
  QAction* newLayerAction = mLayerToolBar->addWidget( bt );

  newLayerAction->setObjectName( "ActionNewLayer" );
  connect( bt, SIGNAL( triggered( QAction * ) ), this,
           SLOT( toolButtonActionTriggered( QAction * ) ) );

  //circular string digitize tool button
  QToolButton* tbAddCircularString = new QToolButton( mDigitizeToolBar );
  tbAddCircularString->setPopupMode( QToolButton::MenuButtonPopup );
  tbAddCircularString->addAction( mActionCircularStringCurvePoint );
  tbAddCircularString->addAction( mActionCircularStringRadius );
  tbAddCircularString->setDefaultAction( mActionCircularStringCurvePoint );
  connect( tbAddCircularString, SIGNAL( triggered( QAction * ) ), this,
           SLOT( toolButtonActionTriggered( QAction * ) ) );
  mDigitizeToolBar->insertWidget( mActionMoveFeature, tbAddCircularString );

  // Cad toolbar
  mAdvancedDigitizeToolBar->insertAction( mActionEnableTracing,
                                mAdvancedDigitizingDockWidget->enableAction() );

  mTracer = new QgsMapCanvasTracer( mMapCanvas, messageBar() );
  mTracer->setActionEnableTracing( mActionEnableTracing );
}

void NGQgisApp::createStatusBar()
{
  //remove borders from children under Windows
  statusBar()->setStyleSheet( "QStatusBar::item {border: none;}" );

  // Add a panel to the status bar for the scale, coords and progress
  // And also rendering suppression checkbox
  mProgressBar = new QProgressBar( statusBar() );
  mProgressBar->setObjectName( "mProgressBar" );
  mProgressBar->setMaximumWidth( 100 );
  mProgressBar->hide();
  mProgressBar->setWhatsThis( tr( "Progress bar that displays the status "
                                  "of rendering layers and other time-intensive operations" ) );
  statusBar()->addPermanentWidget( mProgressBar, 1 );

  connect( mMapCanvas, SIGNAL( renderStarting() ), this,
           SLOT( canvasRefreshStarted() ) );
  connect( mMapCanvas, SIGNAL( mapCanvasRefreshed() ), this,
           SLOT( canvasRefreshFinished() ) );

  // Bumped the font up one point size since 8 was too
  // small on some platforms. A point size of 9 still provides
  // plenty of display space on 1024x768 resolutions
  QFont myFont( "Arial", 9 );
  statusBar()->setFont( myFont );

  //coords status bar widget
  mCoordsEdit = new QgsStatusBarCoordinatesWidget( statusBar() );
  mCoordsEdit->setMapCanvas( mMapCanvas );
  mCoordsEdit->setFont( myFont );
  statusBar()->addPermanentWidget( mCoordsEdit, 0 );

  // add a label to show current scale
  mScaleLabel = new QLabel( QString(), statusBar() );
  mScaleLabel->setObjectName( "mScaleLable" );
  mScaleLabel->setFont( myFont );
  mScaleLabel->setMinimumWidth( 10 );
  //mScaleLabel->setMaximumHeight( 20 );
  mScaleLabel->setMargin( 3 );
  mScaleLabel->setAlignment( Qt::AlignCenter );
  mScaleLabel->setFrameStyle( QFrame::NoFrame );
  mScaleLabel->setText( tr( "Scale" ) );
  mScaleLabel->setToolTip( tr( "Current map scale" ) );
  statusBar()->addPermanentWidget( mScaleLabel, 0 );

  mScaleEdit = new QgsScaleComboBox( statusBar() );
  mScaleEdit->setObjectName( "mScaleEdit" );
  mScaleEdit->setFont( myFont );
  // seems setFont() change font only for popup not for line edit,
  // so we need to set font for it separately
  mScaleEdit->lineEdit()->setFont( myFont );
  mScaleEdit->setMinimumWidth( 10 );
  mScaleEdit->setContentsMargins( 0, 0, 0, 0 );
  mScaleEdit->setWhatsThis( tr( "Displays the current map scale" ) );
  mScaleEdit->setToolTip( tr( "Current map scale (formatted as x:y)" ) );

  statusBar()->addPermanentWidget( mScaleEdit, 0 );
  connect( mScaleEdit, SIGNAL( scaleChanged() ), this, SLOT( userScale() ) );

  if ( QgsMapCanvas::rotationEnabled() )
  {
    // add a widget to show/set current rotation
    mRotationLabel = new QLabel( QString(), statusBar() );
    mRotationLabel->setObjectName( "mRotationLabel" );
    mRotationLabel->setFont( myFont );
    mRotationLabel->setMinimumWidth( 10 );
    //mRotationLabel->setMaximumHeight( 20 );
    mRotationLabel->setMargin( 3 );
    mRotationLabel->setAlignment( Qt::AlignCenter );
    mRotationLabel->setFrameStyle( QFrame::NoFrame );
    mRotationLabel->setText( tr( "Rotation" ) );
    mRotationLabel->setToolTip( tr( "Current clockwise map rotation in degrees" ) );
    statusBar()->addPermanentWidget( mRotationLabel, 0 );

    mRotationEdit = new QgsDoubleSpinBox( statusBar() );
    mRotationEdit->setObjectName( "mRotationEdit" );
    mRotationEdit->setClearValue( 0.0 );
    mRotationEdit->setKeyboardTracking( false );
    mRotationEdit->setMaximumWidth( 120 );
    mRotationEdit->setDecimals( 1 );
    mRotationEdit->setRange( -180.0, 180.0 );
    mRotationEdit->setWrapping( true );
    mRotationEdit->setSingleStep( 5.0 );
    mRotationEdit->setFont( myFont );
    mRotationEdit->setWhatsThis( tr( "Shows the current map clockwise rotation "
                                     "in degrees. It also allows editing to set "
                                     "the rotation" ) );
    mRotationEdit->setToolTip( tr( "Current clockwise map rotation in degrees" ) );
    statusBar()->addPermanentWidget( mRotationEdit, 0 );
    connect( mRotationEdit, SIGNAL( valueChanged( double ) ), this, SLOT( userRotation() ) );

    showRotation();
  }

  // render suppression status bar widget
  mRenderSuppressionCBox = new QCheckBox( tr( "Render" ), statusBar() );
  mRenderSuppressionCBox->setObjectName( "mRenderSuppressionCBox" );
  mRenderSuppressionCBox->setChecked( true );
  mRenderSuppressionCBox->setFont( myFont );
  mRenderSuppressionCBox->setWhatsThis( tr( "When checked, the map layers "
                                        "are rendered in response to map navigation commands and other "
                                        "events. When not checked, no rendering is done. This allows you "
                                        "to add a large number of layers and symbolize them before rendering." ) );
  mRenderSuppressionCBox->setToolTip( tr( "Toggle map rendering" ) );
  statusBar()->addPermanentWidget( mRenderSuppressionCBox, 0 );
  // On the fly projection status bar icon
  // Changed this to a tool button since a QPushButton is
  // sculpted on OS X and the icon is never displayed [gsherman]
  mOnTheFlyProjectionStatusButton = new QToolButton( statusBar() );
  mOnTheFlyProjectionStatusButton->setAutoRaise( true );
  mOnTheFlyProjectionStatusButton->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
  mOnTheFlyProjectionStatusButton->setObjectName( "mOntheFlyProjectionStatusButton" );
  // Maintain uniform widget height in status bar by setting button height same as labels
  // For Qt/Mac 3.3, the default toolbutton height is 30 and labels were expanding to match
  mOnTheFlyProjectionStatusButton->setMaximumHeight( mScaleLabel->height() );
  mOnTheFlyProjectionStatusButton->setIcon( NGQgsApplication::getThemeIcon( "mIconProjectionEnabled.png" ) );
  mOnTheFlyProjectionStatusButton->setWhatsThis( tr( "This icon shows whether "
      "on the fly coordinate reference system transformation is enabled or not. "
      "Click the icon to bring up "
      "the project properties dialog to alter this behaviour." ) );
  mOnTheFlyProjectionStatusButton->setToolTip( tr( "CRS status - Click "
      "to open coordinate reference system dialog" ) );
  connect( mOnTheFlyProjectionStatusButton, SIGNAL( clicked() ),
           this, SLOT( projectPropertiesProjections() ) );//bring up the project props dialog when clicked
  statusBar()->addPermanentWidget( mOnTheFlyProjectionStatusButton, 0 );
  statusBar()->showMessage( tr( "Ready" ) );

  mMessageButton = new QToolButton( statusBar() );
  mMessageButton->setAutoRaise( true );
  mMessageButton->setIcon( NGQgsApplication::getThemeIcon( "/mMessageLogRead.svg" ) );
  mMessageButton->setToolTip( tr( "Messages" ) );
  mMessageButton->setWhatsThis( tr( "Messages" ) );
  mMessageButton->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
  mMessageButton->setObjectName( "mMessageLogViewerButton" );
  mMessageButton->setMaximumHeight( mScaleLabel->height() );
  mMessageButton->setCheckable( true );
  statusBar()->addPermanentWidget( mMessageButton, 0 );
}

void NGQgisApp::readSettings()
{
  QSettings settings;
  QString themename = settings.value( "UI/UITheme", "default" ).toString();
  setTheme( themename );

  // Read legacy settings
  mRecentProjects.clear();

  settings.beginGroup( "/UI" );

  // Migrate old recent projects if first time with new system
  if ( !settings.childGroups().contains( "recentProjects" ) )
  {
    QStringList oldRecentProjects = settings.value( "/UI/recentProjectsList" ).toStringList();

    Q_FOREACH ( const QString& project, oldRecentProjects )
    {
      QgsWelcomePageItemsModel::RecentProjectData data;
      data.path = project;
      data.title = project;

      mRecentProjects.append( data );
    }
  }
  settings.endGroup();

  settings.beginGroup( "/UI/recentProjects" );
  QStringList projectKeysList = settings.childGroups();

  //convert list to int values to obtain proper order
  QList<int> projectKeys;
  Q_FOREACH ( const QString& key, projectKeysList )
  {
    projectKeys.append( key.toInt() );
  }
  qSort( projectKeys );

  Q_FOREACH ( int key, projectKeys )
  {
    QgsWelcomePageItemsModel::RecentProjectData data;
    settings.beginGroup( QString::number( key ) );
    data.title = settings.value( "title" ).toString();
    data.path = settings.value( "path" ).toString();
    data.previewImagePath = settings.value( "previewImage" ).toString();
    data.crs = settings.value( "crs" ).toString();
    settings.endGroup();
    mRecentProjects.append( data );
  }
  settings.endGroup();

  // this is a new session! reset enable macros value to "ask"
  // whether set to "just for this session"
  if ( settings.value( "/qgis/enableMacros", 1 ).toInt() == 2 )
  {
    settings.setValue( "/qgis/enableMacros", 1 );
  }
}

void NGQgisApp::setTheme( const QString& theThemeName )
{
  NGQgsApplication::setUITheme( theThemeName );
  //QgsDebugMsg("Setting theme to \n" + theThemeName);
  mActionNewProject->setIcon( NGQgsApplication::getThemeIcon( "/mActionFileNew.svg" ) );
  mActionOpenProject->setIcon( NGQgsApplication::getThemeIcon( "/mActionFileOpen.svg" ) );
  mActionSaveProject->setIcon( NGQgsApplication::getThemeIcon( "/mActionFileSave.svg" ) );
  mActionSaveProjectAs->setIcon( NGQgsApplication::getThemeIcon( "/mActionFileSaveAs.svg" ) );
  mActionNewPrintComposer->setIcon( NGQgsApplication::getThemeIcon( "/mActionNewComposer.svg" ) );
  mActionShowComposerManager->setIcon( NGQgsApplication::getThemeIcon( "/mActionComposerManager.svg" ) );
  mActionSaveMapAsImage->setIcon( NGQgsApplication::getThemeIcon( "/mActionSaveMapAsImage.png" ) );
  mActionExit->setIcon( NGQgsApplication::getThemeIcon( "/mActionFileExit.png" ) );
  mActionAddOgrLayer->setIcon( NGQgsApplication::getThemeIcon( "/mActionAddOgrLayer.svg" ) );
  mActionAddRasterLayer->setIcon( NGQgsApplication::getThemeIcon( "/mActionAddRasterLayer.svg" ) );
#ifdef HAVE_POSTGRESQL
  mActionAddPgLayer->setIcon( NGQgsApplication::getThemeIcon( "/mActionAddPostgisLayer.svg" ) );
#endif
  mActionNewSpatiaLiteLayer->setIcon( NGQgsApplication::getThemeIcon( "/mActionNewSpatiaLiteLayer.svg" ) );
  mActionAddSpatiaLiteLayer->setIcon( NGQgsApplication::getThemeIcon( "/mActionAddSpatiaLiteLayer.svg" ) );
#ifdef HAVE_MSSQL
  mActionAddMssqlLayer->setIcon( NGQgsApplication::getThemeIcon( "/mActionAddMssqlLayer.svg" ) );
#endif
#ifdef HAVE_ORACLE
  mActionAddOracleLayer->setIcon( NGQgsApplication::getThemeIcon( "/mActionAddOracleLayer.svg" ) );
#endif
  mActionRemoveLayer->setIcon( NGQgsApplication::getThemeIcon( "/mActionRemoveLayer.svg" ) );
  mActionDuplicateLayer->setIcon( NGQgsApplication::getThemeIcon( "/mActionDuplicateLayer.svg" ) );
  mActionSetLayerCRS->setIcon( NGQgsApplication::getThemeIcon( "/mActionSetLayerCRS.png" ) );
  mActionSetProjectCRSFromLayer->setIcon( NGQgsApplication::getThemeIcon( "/mActionSetProjectCRSFromLayer.png" ) );
  mActionNewVectorLayer->setIcon( NGQgsApplication::getThemeIcon( "/mActionNewVectorLayer.svg" ) );
  mActionNewMemoryLayer->setIcon( NGQgsApplication::getThemeIcon( "/mActionCreateMemory.svg" ) );
  mActionAddAllToOverview->setIcon( NGQgsApplication::getThemeIcon( "/mActionAddAllToOverview.svg" ) );
  mActionHideAllLayers->setIcon( NGQgsApplication::getThemeIcon( "/mActionHideAllLayers.png" ) );
  mActionShowAllLayers->setIcon( NGQgsApplication::getThemeIcon( "/mActionShowAllLayers.png" ) );
  mActionHideSelectedLayers->setIcon( NGQgsApplication::getThemeIcon( "/mActionHideSelectedLayers.png" ) );
  mActionShowSelectedLayers->setIcon( NGQgsApplication::getThemeIcon( "/mActionShowSelectedLayers.png" ) );
  mActionRemoveAllFromOverview->setIcon( NGQgsApplication::getThemeIcon( "/mActionRemoveAllFromOverview.svg" ) );
  mActionToggleFullScreen->setIcon( NGQgsApplication::getThemeIcon( "/mActionToggleFullScreen.png" ) );
  mActionProjectProperties->setIcon( NGQgsApplication::getThemeIcon( "/mActionProjectProperties.png" ) );
  mActionManagePlugins->setIcon( NGQgsApplication::getThemeIcon( "/mActionShowPluginManager.svg" ) );
  mActionShowPythonDialog->setIcon( NGQgsApplication::getThemeIcon( "console/iconRunConsole.png" ) );
  mActionCheckQgisVersion->setIcon( NGQgsApplication::getThemeIcon( "/mActionCheckQgisVersion.png" ) );
  mActionOptions->setIcon( NGQgsApplication::getThemeIcon( "/mActionOptions.svg" ) );
  mActionConfigureShortcuts->setIcon( NGQgsApplication::getThemeIcon( "/mActionOptions.svg" ) );
//  mActionCustomization->setIcon( NGQgsApplication::getThemeIcon( "/mActionOptions.svg" ) );
  mActionHelpContents->setIcon( NGQgsApplication::getThemeIcon( "/mActionHelpContents.svg" ) );
  mActionLocalHistogramStretch->setIcon( NGQgsApplication::getThemeIcon( "/mActionLocalHistogramStretch.png" ) );
  mActionFullHistogramStretch->setIcon( NGQgsApplication::getThemeIcon( "/mActionFullHistogramStretch.png" ) );
  mActionIncreaseBrightness->setIcon( NGQgsApplication::getThemeIcon( "/mActionIncreaseBrightness.svg" ) );
  mActionDecreaseBrightness->setIcon( NGQgsApplication::getThemeIcon( "/mActionDecreaseBrightness.svg" ) );
  mActionIncreaseContrast->setIcon( NGQgsApplication::getThemeIcon( "/mActionIncreaseContrast.svg" ) );
  mActionDecreaseContrast->setIcon( NGQgsApplication::getThemeIcon( "/mActionDecreaseContrast.svg" ) );
  mActionZoomActualSize->setIcon( NGQgsApplication::getThemeIcon( "/mActionZoomNative.png" ) );
  mActionQgisHomePage->setIcon( NGQgsApplication::getThemeIcon( "/mActionQgisHomePage.png" ) );
  mActionAbout->setIcon( NGQgsApplication::getThemeIcon( "/mActionHelpAbout.png" ) );
//  mActionSponsors->setIcon( NGQgsApplication::getThemeIcon( "/mActionHelpSponsors.png" ) );
  mActionDraw->setIcon( NGQgsApplication::getThemeIcon( "/mActionDraw.svg" ) );
  mActionToggleEditing->setIcon( NGQgsApplication::getThemeIcon( "/mActionToggleEditing.svg" ) );
  mActionSaveLayerEdits->setIcon( NGQgsApplication::getThemeIcon( "/mActionSaveAllEdits.svg" ) );
  mActionAllEdits->setIcon( NGQgsApplication::getThemeIcon( "/mActionAllEdits.svg" ) );
  mActionSaveEdits->setIcon( NGQgsApplication::getThemeIcon( "/mActionSaveEdits.svg" ) );
  mActionSaveAllEdits->setIcon( NGQgsApplication::getThemeIcon( "/mActionSaveAllEdits.svg" ) );
  mActionRollbackEdits->setIcon( NGQgsApplication::getThemeIcon( "/mActionRollbackEdits.svg" ) );
  mActionRollbackAllEdits->setIcon( NGQgsApplication::getThemeIcon( "/mActionRollbackAllEdits.svg" ) );
  mActionCancelEdits->setIcon( NGQgsApplication::getThemeIcon( "/mActionCancelEdits.svg" ) );
  mActionCancelAllEdits->setIcon( NGQgsApplication::getThemeIcon( "/mActionCancelAllEdits.svg" ) );
  mActionCutFeatures->setIcon( NGQgsApplication::getThemeIcon( "/mActionEditCut.png" ) );
  mActionCopyFeatures->setIcon( NGQgsApplication::getThemeIcon( "/mActionEditCopy.png" ) );
  mActionPasteFeatures->setIcon( NGQgsApplication::getThemeIcon( "/mActionEditPaste.png" ) );
  mActionAddFeature->setIcon( NGQgsApplication::getThemeIcon( "/mActionCapturePoint.png" ) );
  mActionMoveFeature->setIcon( NGQgsApplication::getThemeIcon( "/mActionMoveFeature.png" ) );
  mActionRotateFeature->setIcon( NGQgsApplication::getThemeIcon( "/mActionRotateFeature.png" ) );
  mActionReshapeFeatures->setIcon( NGQgsApplication::getThemeIcon( "/mActionReshape.png" ) );
  mActionSplitFeatures->setIcon( NGQgsApplication::getThemeIcon( "/mActionSplitFeatures.svg" ) );
  mActionSplitParts->setIcon( NGQgsApplication::getThemeIcon( "/mActionSplitParts.svg" ) );
  mActionDeleteSelected->setIcon( NGQgsApplication::getThemeIcon( "/mActionDeleteSelected.svg" ) );
  mActionNodeTool->setIcon( NGQgsApplication::getThemeIcon( "/mActionNodeTool.png" ) );
  mActionSimplifyFeature->setIcon( NGQgsApplication::getThemeIcon( "/mActionSimplify.png" ) );
  mActionUndo->setIcon( NGQgsApplication::getThemeIcon( "/mActionUndo.png" ) );
  mActionRedo->setIcon( NGQgsApplication::getThemeIcon( "/mActionRedo.png" ) );
  mActionAddRing->setIcon( NGQgsApplication::getThemeIcon( "/mActionAddRing.png" ) );
  mActionFillRing->setIcon( NGQgsApplication::getThemeIcon( "/mActionFillRing.svg" ) );
  mActionAddPart->setIcon( NGQgsApplication::getThemeIcon( "/mActionAddPart.png" ) );
  mActionDeleteRing->setIcon( NGQgsApplication::getThemeIcon( "/mActionDeleteRing.png" ) );
  mActionDeletePart->setIcon( NGQgsApplication::getThemeIcon( "/mActionDeletePart.png" ) );
  mActionMergeFeatures->setIcon( NGQgsApplication::getThemeIcon( "/mActionMergeFeatures.png" ) );
  mActionOffsetCurve->setIcon( NGQgsApplication::getThemeIcon( "/mActionOffsetCurve.png" ) );
  mActionMergeFeatureAttributes->setIcon( NGQgsApplication::getThemeIcon( "/mActionMergeFeatureAttributes.png" ) );
  mActionRotatePointSymbols->setIcon( NGQgsApplication::getThemeIcon( "mActionRotatePointSymbols.png" ) );
  mActionZoomIn->setIcon( NGQgsApplication::getThemeIcon( "/mActionZoomIn.svg" ) );
  mActionZoomOut->setIcon( NGQgsApplication::getThemeIcon( "/mActionZoomOut.svg" ) );
  mActionZoomFullExtent->setIcon( NGQgsApplication::getThemeIcon( "/mActionZoomFullExtent.svg" ) );
  mActionZoomToSelected->setIcon( NGQgsApplication::getThemeIcon( "/mActionZoomToSelected.svg" ) );
  mActionShowRasterCalculator->setIcon( NGQgsApplication::getThemeIcon( "/mActionShowRasterCalculator.png" ) );
  mActionPan->setIcon( NGQgsApplication::getThemeIcon( "/mActionPan.svg" ) );
  mActionPanToSelected->setIcon( NGQgsApplication::getThemeIcon( "/mActionPanToSelected.svg" ) );
  mActionZoomLast->setIcon( NGQgsApplication::getThemeIcon( "/mActionZoomLast.svg" ) );
  mActionZoomNext->setIcon( NGQgsApplication::getThemeIcon( "/mActionZoomNext.svg" ) );
  mActionZoomToLayer->setIcon( NGQgsApplication::getThemeIcon( "/mActionZoomToLayer.svg" ) );
  mActionZoomActualSize->setIcon( NGQgsApplication::getThemeIcon( "/mActionZoomActual.svg" ) );
  mActionIdentify->setIcon( NGQgsApplication::getThemeIcon( "/mActionIdentify.svg" ) );
  mActionFeatureAction->setIcon( NGQgsApplication::getThemeIcon( "/mAction.svg" ) );
  mActionSelectFeatures->setIcon( NGQgsApplication::getThemeIcon( "/mActionSelectRectangle.svg" ) );
  mActionSelectPolygon->setIcon( NGQgsApplication::getThemeIcon( "/mActionSelectPolygon.svg" ) );
  mActionSelectFreehand->setIcon( NGQgsApplication::getThemeIcon( "/mActionSelectFreehand.svg" ) );
  mActionSelectRadius->setIcon( NGQgsApplication::getThemeIcon( "/mActionSelectRadius.svg" ) );
  mActionDeselectAll->setIcon( NGQgsApplication::getThemeIcon( "/mActionDeselectAll.svg" ) );
  mActionSelectAll->setIcon( NGQgsApplication::getThemeIcon( "/mActionSelectAll.svg" ) );
  mActionInvertSelection->setIcon( NGQgsApplication::getThemeIcon( "/mActionInvertSelection.svg" ) );
  mActionSelectByExpression->setIcon( NGQgsApplication::getThemeIcon( "/mIconExpressionSelect.svg" ) );
  mActionOpenTable->setIcon( NGQgsApplication::getThemeIcon( "/mActionOpenTable.svg" ) );
  mActionOpenFieldCalc->setIcon( NGQgsApplication::getThemeIcon( "/mActionCalculateField.png" ) );
  mActionMeasure->setIcon( NGQgsApplication::getThemeIcon( "/mActionMeasure.png" ) );
  mActionMeasureArea->setIcon( NGQgsApplication::getThemeIcon( "/mActionMeasureArea.png" ) );
  mActionMeasureAngle->setIcon( NGQgsApplication::getThemeIcon( "/mActionMeasureAngle.png" ) );
  mActionMapTips->setIcon( NGQgsApplication::getThemeIcon( "/mActionMapTips.png" ) );
  mActionShowBookmarks->setIcon( NGQgsApplication::getThemeIcon( "/mActionShowBookmarks.png" ) );
  mActionNewBookmark->setIcon( NGQgsApplication::getThemeIcon( "/mActionNewBookmark.png" ) );
  mActionCustomProjection->setIcon( NGQgsApplication::getThemeIcon( "/mActionCustomProjection.svg" ) );
  mActionAddWmsLayer->setIcon( NGQgsApplication::getThemeIcon( "/mActionAddWmsLayer.svg" ) );
  mActionAddWcsLayer->setIcon( NGQgsApplication::getThemeIcon( "/mActionAddWcsLayer.svg" ) );
  mActionAddWfsLayer->setIcon( NGQgsApplication::getThemeIcon( "/mActionAddWfsLayer.svg" ) );
  mActionAddToOverview->setIcon( NGQgsApplication::getThemeIcon( "/mActionInOverview.svg" ) );
  mActionAnnotation->setIcon( NGQgsApplication::getThemeIcon( "/mActionAnnotation.png" ) );
  mActionFormAnnotation->setIcon( NGQgsApplication::getThemeIcon( "/mActionFormAnnotation.png" ) );
  mActionHtmlAnnotation->setIcon( NGQgsApplication::getThemeIcon( "/mActionFormAnnotation.png" ) );
  mActionTextAnnotation->setIcon( NGQgsApplication::getThemeIcon( "/mActionTextAnnotation.png" ) );
  mActionLabeling->setIcon( NGQgsApplication::getThemeIcon( "/mActionLabeling.png" ) );
  mActionShowPinnedLabels->setIcon( NGQgsApplication::getThemeIcon( "/mActionShowPinnedLabels.svg" ) );
  mActionPinLabels->setIcon( NGQgsApplication::getThemeIcon( "/mActionPinLabels.svg" ) );
  mActionShowHideLabels->setIcon( NGQgsApplication::getThemeIcon( "/mActionShowHideLabels.svg" ) );
  mActionMoveLabel->setIcon( NGQgsApplication::getThemeIcon( "/mActionMoveLabel.png" ) );
  mActionRotateLabel->setIcon( NGQgsApplication::getThemeIcon( "/mActionRotateLabel.svg" ) );
  mActionChangeLabelProperties->setIcon( NGQgsApplication::getThemeIcon( "/mActionChangeLabelProperties.png" ) );
  mActionDecorationCopyright->setIcon( NGQgsApplication::getThemeIcon( "/copyright_label.png" ) );
  mActionDecorationNorthArrow->setIcon( NGQgsApplication::getThemeIcon( "/north_arrow.png" ) );
  mActionDecorationScaleBar->setIcon( NGQgsApplication::getThemeIcon( "/scale_bar.png" ) );
  mActionDecorationGrid->setIcon( NGQgsApplication::getThemeIcon( "/transformed.png" ) );

  //change themes of all composers
  QSet<QgsComposer*>::const_iterator composerIt = mPrintComposers.constBegin();
  for ( ; composerIt != mPrintComposers.constEnd(); ++composerIt )
  {
    ( *composerIt )->setupTheme();
  }

  emit currentThemeChanged( theThemeName );
}

void NGQgisApp::updateProjectFromTemplates()
{
    // get list of project files in template dir
    QSettings settings;
    QString templateDirName = settings.value( "/qgis/projectTemplateDir",
                              NGQgsApplication::qgisSettingsDirPath() +
                                              "project_templates" ).toString();
    QDir templateDir( templateDirName );
    QStringList filters( "*.qgs" );
    templateDir.setNameFilters( filters );
    QStringList templateFiles = templateDir.entryList( filters );

    // Remove existing entries
    mProjectFromTemplateMenu->clear();

    // Add entries
    Q_FOREACH ( const QString& templateFile, templateFiles )
    {
      mProjectFromTemplateMenu->addAction( templateFile );
    }

    // add <blank> entry, which loads a blank template (regardless of "default template")
    if ( settings.value( "/qgis/newProjectDefault", QVariant( false ) ).toBool() )
        mProjectFromTemplateMenu->addAction( tr( "< Blank >" ) );
}

void NGQgisApp::toggleLogMessageIcon( bool hasLogMessage )
{
  if ( hasLogMessage && !mLogDock->isVisible() )
  {
    mMessageButton->setIcon( NGQgsApplication::getThemeIcon( "/mMessageLog.svg" ) );
  }
  else
  {
    mMessageButton->setIcon( NGQgsApplication::getThemeIcon( "/mMessageLogRead.svg" ) );
  }
}

void NGQgisApp::initLayerTreeView()
{
  mLayerTreeView->setWhatsThis( tr( "Map legend that displays all the layers currently on the map canvas. Click on the check box to turn a layer on or off. Double click on a layer in the legend to customize its appearance and set other properties." ) );

  mLayerTreeDock = new QDockWidget( tr( "Layers Panel" ), this );
  mLayerTreeDock->setObjectName( "Layers" );
  mLayerTreeDock->setAllowedAreas( Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea );

  QgsLayerTreeModel *model = new QgsLayerTreeModel(
              QgsProject::instance()->layerTreeRoot(), this );

  model->setFlag( QgsLayerTreeModel::AllowNodeReorder );
  model->setFlag( QgsLayerTreeModel::AllowNodeRename );
  model->setFlag( QgsLayerTreeModel::AllowNodeChangeVisibility );
  model->setFlag( QgsLayerTreeModel::ShowLegendAsTree );
  model->setAutoCollapseLegendNodes( 10 );

  mLayerTreeView->setModel( model );
  mLayerTreeView->setMenuProvider( new QgsAppLayerTreeViewMenuProvider(
                                       mLayerTreeView, mMapCanvas ) );

  setupLayerTreeViewFromSettings();

  connect( mLayerTreeView, SIGNAL( doubleClicked( QModelIndex ) ), this,
           SLOT( layerTreeViewDoubleClicked( QModelIndex ) ) );
  connect( mLayerTreeView, SIGNAL( currentLayerChanged( QgsMapLayer* ) ), this,
           SLOT( activeLayerChanged( QgsMapLayer* ) ) );
  connect( mLayerTreeView->selectionModel(),
           SIGNAL( currentChanged( QModelIndex, QModelIndex ) ),
           this, SLOT( updateNewLayerInsertionPoint() ) );
  connect( QgsProject::instance()->layerTreeRegistryBridge(),
           SIGNAL( addedLayersToLayerTree( QList<QgsMapLayer*> ) ),
           this, SLOT( autoSelectAddedLayer( QList<QgsMapLayer*> ) ) );

  // add group action
  QAction* actionAddGroup = new QAction( tr( "Add Group" ), this );
  actionAddGroup->setIcon( NGQgsApplication::getThemeIcon( "/mActionAddGroup.svg" ) );
  actionAddGroup->setToolTip( tr( "Add Group" ) );
  connect( actionAddGroup, SIGNAL( triggered( bool ) ),
           mLayerTreeView->defaultActions(), SLOT( addGroup() ) );

  // visibility groups tool button
  QToolButton* btnVisibilityPresets = new QToolButton;
  btnVisibilityPresets->setAutoRaise( true );
  btnVisibilityPresets->setToolTip( tr( "Manage Layer Visibility" ) );
  btnVisibilityPresets->setIcon( NGQgsApplication::getThemeIcon( "/mActionShowAllLayers.svg" ) );
  btnVisibilityPresets->setPopupMode( QToolButton::InstantPopup );
  btnVisibilityPresets->setMenu( QgsVisibilityPresets::instance()->menu() );

  // filter legend action
  mActionFilterLegend = new QAction( tr( "Filter Legend By Map Content" ), this );
  mActionFilterLegend->setCheckable( true );
  mActionFilterLegend->setToolTip( tr( "Filter Legend By Map Content" ) );
  mActionFilterLegend->setIcon( NGQgsApplication::getThemeIcon( "/mActionFilter2.svg" ) );
  connect( mActionFilterLegend, SIGNAL( toggled( bool ) ), this, SLOT( updateFilterLegend() ) );

  mLegendExpressionFilterButton = new QgsLegendFilterButton( this );
  mLegendExpressionFilterButton->setToolTip( tr( "Filter legend by expression" ) );
  connect( mLegendExpressionFilterButton, SIGNAL( toggled( bool ) ), this,
           SLOT( toggleFilterLegendByExpression( bool ) ) );

  // expand / collapse tool buttons
  QAction* actionExpandAll = new QAction( tr( "Expand All" ), this );
  actionExpandAll->setIcon( NGQgsApplication::getThemeIcon( "/mActionExpandTree.svg" ) );
  actionExpandAll->setToolTip( tr( "Expand All" ) );
  connect( actionExpandAll, SIGNAL( triggered( bool ) ), mLayerTreeView, SLOT( expandAllNodes() ) );
  QAction* actionCollapseAll = new QAction( tr( "Collapse All" ), this );
  actionCollapseAll->setIcon( NGQgsApplication::getThemeIcon( "/mActionCollapseTree.svg" ) );
  actionCollapseAll->setToolTip( tr( "Collapse All" ) );
  connect( actionCollapseAll, SIGNAL( triggered( bool ) ), mLayerTreeView, SLOT( collapseAllNodes() ) );

  QToolBar* toolbar = new QToolBar();
  toolbar->setIconSize( QSize( 16, 16 ) );
  toolbar->addAction( actionAddGroup );
  toolbar->addWidget( btnVisibilityPresets );
  toolbar->addAction( mActionFilterLegend );
  toolbar->addWidget( mLegendExpressionFilterButton );
  toolbar->addAction( actionExpandAll );
  toolbar->addAction( actionCollapseAll );
  toolbar->addAction( mActionRemoveLayer );

  QVBoxLayout* vboxLayout = new QVBoxLayout;
  vboxLayout->setMargin( 0 );
  vboxLayout->addWidget( toolbar );
  vboxLayout->addWidget( mLayerTreeView );

  QWidget* w = new QWidget;
  w->setLayout( vboxLayout );
  mLayerTreeDock->setWidget( w );
  addDockWidget( Qt::LeftDockWidgetArea, mLayerTreeDock );

  mLayerTreeCanvasBridge = new QgsLayerTreeMapCanvasBridge(
              QgsProject::instance()->layerTreeRoot(), mMapCanvas, this );
  connect( QgsProject::instance(), SIGNAL( writeProject( QDomDocument& ) ),
           mLayerTreeCanvasBridge, SLOT( writeProject( QDomDocument& ) ) );
  connect( QgsProject::instance(), SIGNAL( readProject( QDomDocument ) ),
           mLayerTreeCanvasBridge, SLOT( readProject( QDomDocument ) ) );

  bool otfTransformAutoEnable = QSettings().value(
              "/Projections/otfTransformAutoEnable", true ).toBool();
  mLayerTreeCanvasBridge->setAutoEnableCrsTransform( otfTransformAutoEnable );

  mMapLayerOrder = new QgsCustomLayerOrderWidget( mLayerTreeCanvasBridge, this );
  mMapLayerOrder->setObjectName( "theMapLayerOrder" );

  mMapLayerOrder->setWhatsThis( tr( "Map layer list that displays all layers in drawing order." ) );
  mLayerOrderDock = new QDockWidget( tr( "Layer Order Panel" ), this );
  mLayerOrderDock->setObjectName( "LayerOrder" );
  mLayerOrderDock->setAllowedAreas( Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea );

  mLayerOrderDock->setWidget( mMapLayerOrder );
  addDockWidget( Qt::LeftDockWidgetArea, mLayerOrderDock );
  mLayerOrderDock->hide();

  connect( mMapCanvas, SIGNAL( mapCanvasRefreshed() ), this, SLOT( updateFilterLegend() ) );
}

void NGQgisApp::saveRecentProjectPath( const QString& projectPath, bool savePreviewImage )
{
  QSettings settings;

  // Get canonical absolute path
  QFileInfo myFileInfo( projectPath );
  QgsWelcomePageItemsModel::RecentProjectData projectData;
  projectData.path = myFileInfo.absoluteFilePath();
  projectData.title = QgsProject::instance()->title();
  if ( projectData.title.isEmpty() )
    projectData.title = projectData.path;

  projectData.crs = mMapCanvas->mapSettings().destinationCrs().authid();

  if ( savePreviewImage )
  {
    // Generate a unique file name
    QString fileName( QCryptographicHash::hash(( projectData.path.toUtf8() ),
                                               QCryptographicHash::Md5 ).toHex() );
    QString previewDir = QString( "%1/previewImages" ).arg(
                NGQgsApplication::qgisSettingsDirPath() );
    projectData.previewImagePath = QString( "%1/%2.png" ).arg( previewDir, fileName );
    QDir().mkdir( previewDir );

    // Render the map canvas
    QSize previewSize( 250, 177 ); // h = w / sqrt(2)
    QRect previewRect( QPoint(( mMapCanvas->width() - previewSize.width() ) / 2
                              , ( mMapCanvas->height() - previewSize.height() ) / 2 )
                       , previewSize );

    QPixmap previewImage( previewSize );
    QPainter previewPainter( &previewImage );
    mMapCanvas->render( &previewPainter, QRect( QPoint(), previewSize ), previewRect );

    // Save
    previewImage.save( projectData.previewImagePath );
  }
  else
  {
    int idx = mRecentProjects.indexOf( projectData );
    if ( idx != -1 )
      projectData.previewImagePath = mRecentProjects.at( idx ).previewImagePath;
  }

  // If this file is already in the list, remove it
  mRecentProjects.removeAll( projectData );

  // Prepend this file to the list
  mRecentProjects.prepend( projectData );

  // Keep the list to 10 items by trimming excess off the bottom
  // And remove the associated image
  while ( mRecentProjects.count() > 10 )
  {
    QFile( mRecentProjects.takeLast().previewImagePath ).remove();
  }

  settings.remove( "/UI/recentProjects" );
  int idx = 0;

  // Persist the list
  Q_FOREACH ( const QgsWelcomePageItemsModel::RecentProjectData& recentProject,
              mRecentProjects )
  {
    ++idx;
    settings.beginGroup( QString( "/UI/recentProjects/%1" ).arg( idx ) );
    settings.setValue( "title", recentProject.title );
    settings.setValue( "path", recentProject.path );
    settings.setValue( "previewImage", recentProject.previewImagePath );
    settings.setValue( "crs", recentProject.crs );
    settings.endGroup();
  }

  // Update menu list of paths
  updateRecentProjectPaths();

}

void NGQgisApp::fileNewFromDefaultTemplate()
{
  QString projectTemplate = NGQgsApplication::qgisSettingsDirPath() +
          QLatin1String( "project_default.qgs" );
  QString msgTxt;
  if ( !projectTemplate.isEmpty() && QFile::exists( projectTemplate ) )
  {
    if ( fileNewFromTemplate( projectTemplate ) )
    {
      return;
    }
    msgTxt = tr( "Default failed to open: %1" );
  }
  else
  {
    msgTxt = tr( "Default not found: %1" );
  }
  messageBar()->pushMessage( tr( "Open Template Project" ),
                             msgTxt.arg( projectTemplate ),
                             QgsMessageBar::WARNING );
}

void NGQgisApp::about()
{
    static NgsAboutDialog *about = new NgsAboutDialog (this);
    about->exec();
}

void NGQgisApp::loadPythonSupport()
{
    QString pythonlibName( "ngqgis_python" );
#if defined(Q_OS_MAC)
    pythonlibName.prepend( NGQgsApplication::libraryPath() + QDir::separator() );
    pythonlibName.append( QLatin1String(".framework") + QDir::separator() +
                          QLatin1String("ngqgis_python") );
#elif defined(Q_OS_LINUX)
    pythonlibName.prepend( NGQgsApplication::libraryPath() );
#elif defined(__MINGW32__)
    pythonlibName.prepend( "lib" );
#endif
  QString version = QString( "%1.%2.%3" ).arg( VERSION_INT / 10000 ).arg( VERSION_INT / 100 % 100 ).arg( VERSION_INT % 100 );
  QgsDebugMsg( QString( "load library %1 (%2)" ).arg( pythonlibName, version ) );

  QLibrary pythonlib( pythonlibName, version );
  // It's necessary to set these two load hints, otherwise Python library won't work correctly
  // see http://lists.kde.org/?l=pykde&m=117190116820758&w=2
  pythonlib.setLoadHints( QLibrary::ResolveAllSymbolsHint |
                          QLibrary::ExportExternalSymbolsHint );
  if ( !pythonlib.load() )
  {
    pythonlib.setFileName( pythonlibName );
    if ( !pythonlib.load() )
    {
      QgsMessageLog::logMessage( tr( "Couldn't load Python support library: %1" ).arg( pythonlib.errorString() ),
        QString::null, QgsMessageLog::WARNING);
      return;
    }
  }

  //QgsDebugMsg("Python support library loaded successfully.");
  typedef QgsPythonUtils*( *inst )();
  inst pythonlib_inst = reinterpret_cast< inst >( cast_to_fptr( pythonlib.resolve( "instance" ) ) );
  if ( !pythonlib_inst )
  {
    //using stderr on purpose because we want end users to see this [TS]
    QgsMessageLog::logMessage( tr( "Couldn't resolve python support library's instance() symbol." ),
      QString::null, QgsMessageLog::WARNING );
    return;
  }

  //QgsDebugMsg("Python support library's instance() symbol resolved.");
  mPythonUtils = pythonlib_inst();
  if ( mPythonUtils )
  {
    mPythonUtils->initPython( mQgisInterface );
  }

  if ( mPythonUtils && mPythonUtils->isEnabled() )
  {
    QgsPluginRegistry::instance()->setPythonUtils( mPythonUtils );

    // init python runner
    QgsPythonRunner::setInstance( new NGQgsPythonRunnerImpl( mPythonUtils ) );

    QgsMessageLog::logMessage( tr( "Python support ENABLED" ), QString::null,
                               QgsMessageLog::INFO );
  }
}

const QString NGQgisApp::updateProgrammPath()
{
  return QFileInfo(
    QApplication::instance()->applicationDirPath()
  ).absolutePath() + QDir::separator() + "nextgisupdater.exe";
}

void NGQgisApp::checkQgisVersion()
{
    // TODO: NextGIS Check
/*  QgsVersionInfo* versionInfo = new QgsVersionInfo();
  QApplication::setOverrideCursor( Qt::WaitCursor );

  connect( versionInfo, SIGNAL( versionInfoAvailable() ), this, SLOT( versionReplyFinished() ) );
  versionInfo->checkVersion();*/
  mUpdatesAvailable = false;

  #ifdef Q_OS_WIN32
    const QString path = updateProgrammPath();
    QStringList args;
    args << "--checkupdates";

    QProcess* process = new QProcess(this);
       
    connect(process, SIGNAL(started()), this, SLOT(maintainerStrated()) );
    connect(process, SIGNAL(error(QProcess::ProcessError)), this, SLOT(maintainerErrored(QProcess::ProcessError)));
    connect(process, SIGNAL(stateChanged(QProcess::ProcessState)), this, SLOT(maintainerStateChanged(QProcess::ProcessState)));
    connect(process, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(maintainerFinished(int, QProcess::ExitStatus)));
    connect(process, SIGNAL(readyReadStandardOutput()), this, SLOT(maintainerReadyReadStandardOutput()));
    connect(process, SIGNAL(readyReadStandardError()), this, SLOT(maintainerReadyReadStandardError()));

    process->start(path, args);
  #endif
}

void NGQgisApp::maintainerStrated()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
}

void NGQgisApp::maintainerErrored(QProcess::ProcessError)
{
}

void NGQgisApp::maintainerStateChanged(QProcess::ProcessState)
{
}

void NGQgisApp::maintainerFinished(int code, QProcess::ExitStatus status)
{
    QApplication::setOverrideCursor(Qt::ArrowCursor);

    QProcess* prc = (QProcess*) sender();
    prc->setParent(NULL);
    
    QWidget* banner = new QWidget(this->messageBar());
    QHBoxLayout* bannerLayout = new QHBoxLayout(banner);
    bannerLayout->setContentsMargins(0, 0, 0, 0);
    bannerLayout->setSpacing(20);

    if ( mUpdatesAvailable )
    {
        QLabel* msg = new QLabel(this->tr("<strong>QGIS updates are available</strong>"), banner);
        msg->setTextFormat(Qt::RichText);
        bannerLayout->addWidget(msg);

        QPushButton* upgrade = new QPushButton("Update", banner);
        bannerLayout->addWidget(upgrade);
        connect(upgrade, SIGNAL(clicked(bool)), this, SLOT(startUpdate()));
    }
    else
    {
        QLabel* msg = new QLabel(this->tr("<strong>There are no available QGIS updates</strong>"), banner);
        msg->setTextFormat(Qt::RichText);
        bannerLayout->addWidget(msg);
    }

    bannerLayout->insertStretch(-1, 1);

    QgsMessageBarItem* item = new QgsMessageBarItem(banner);
    item->setIcon(QIcon(":/images/icons/qgis-icon-60x60.png"));
    this->messageBar()->pushItem(item);
}

void NGQgisApp::maintainerReadyReadStandardOutput()
{
    QProcess* prc = (QProcess*) sender();

    QByteArray data = prc->readAllStandardOutput();
    QXmlInputSource xmlData;
    xmlData.setData(data);
    QXmlSimpleReader reader;
    if ( reader.parse(xmlData) )
    {
      mUpdatesAvailable = true;
    }
}

void NGQgisApp::maintainerReadyReadStandardError()
{
}

void NGQgisApp::startUpdate()
{
  QString program = updateProgrammPath();
  QStringList arguments;
  arguments << "--updater";
  
  QMessageBox::information(this, tr("Attention!"), tr("Please, restart NGQGIS after upgrade."));
  
  QProcess::startDetached(
    program,
    arguments
  ); 
}

void NGQgisApp::increaseBrightness()
{
  int step = 1;
  if ( NGQgsApplication::keyboardModifiers() == Qt::ShiftModifier )
  {
    step = 10;
  }
  adjustBrightnessContrast( step );
}

void NGQgisApp::decreaseBrightness()
{
  int step = -1;
  if ( NGQgsApplication::keyboardModifiers() == Qt::ShiftModifier )
  {
    step = -10;
  }
  adjustBrightnessContrast( step );
}

void NGQgisApp::increaseContrast()
{
  int step = 1;
  if ( NGQgsApplication::keyboardModifiers() == Qt::ShiftModifier )
  {
    step = 10;
  }
  adjustBrightnessContrast( step, false );
}

void NGQgisApp::decreaseContrast()
{
  int step = -1;
  if ( NGQgsApplication::keyboardModifiers() == Qt::ShiftModifier )
  {
    step = -10;
  }
  adjustBrightnessContrast( step, false );
}

void NGQgisApp::helpContents()
{
    openURL( "http://docs.nextgis.ru/docs_ngqgis/source/toc.html", false );
}

void NGQgisApp::apiDocumentation()
{
    // TODO:
}

void NGQgisApp::reportaBug()
{
  openURL( tr( "https://github.com/nextgis/nextgisqgis/issues" ), false );
}

void NGQgisApp::supportProviders()
{
  openURL( tr( "http://nextgis.com" ), false );
}

void NGQgisApp::helpQgisHomePage()
{
  openURL( "http://docs.nextgis.ru/docs_ngqgis/source/toc.html", false );
}

void NGQgisApp::updateCRSStatusBar()
{
  mOnTheFlyProjectionStatusButton->setText(
              mMapCanvas->mapSettings().destinationCrs().authid() );

  if ( mMapCanvas->mapSettings().hasCrsTransformEnabled() )
  {
    mOnTheFlyProjectionStatusButton->setText( tr( "%1 (OTF)" ).arg(
                                    mOnTheFlyProjectionStatusButton->text() ) );
    mOnTheFlyProjectionStatusButton->setToolTip(
      tr( "Current CRS: %1 (OTFR enabled)" ).arg(
                    mMapCanvas->mapSettings().destinationCrs().description() ) );
    mOnTheFlyProjectionStatusButton->setIcon(
                NGQgsApplication::getThemeIcon( "mIconProjectionEnabled.png" ) );
  }
  else
  {
    mOnTheFlyProjectionStatusButton->setToolTip(
      tr( "Current CRS: %1 (OTFR disabled)" ).arg(
                    mMapCanvas->mapSettings().destinationCrs().description() ) );
    mOnTheFlyProjectionStatusButton->setIcon(
                NGQgsApplication::getThemeIcon( "mIconProjectionDisabled.png" ) );
  }
}

void NGQgisApp::activateDeactivateLayerRelatedActions( QgsMapLayer* layer )
{
  bool enableMove = false;
  bool enableRotate = false;
  bool enablePin = false;
  bool enableShowHide = false;
  bool enableChange = false;

  QMap<QString, QgsMapLayer*> layers = QgsMapLayerRegistry::instance()->mapLayers();
  for ( QMap<QString, QgsMapLayer*>::iterator it = layers.begin(); it != layers.end(); ++it )
  {
    QgsVectorLayer *vlayer = qobject_cast<QgsVectorLayer *>( it.value() );
    if ( !vlayer || !vlayer->isEditable() ||
         ( !vlayer->diagramsEnabled() && !vlayer->labelsEnabled() ) )
      continue;

    int colX, colY, colShow, colAng;
    enablePin =
      enablePin ||
      ( qobject_cast<QgsMapToolPinLabels*>( mMapTools.mPinLabels ) &&
        qobject_cast<QgsMapToolPinLabels*>( mMapTools.mPinLabels )->layerCanPin( vlayer, colX, colY ) );

    enableShowHide =
      enableShowHide ||
      ( qobject_cast<QgsMapToolShowHideLabels*>( mMapTools.mShowHideLabels ) &&
        qobject_cast<QgsMapToolShowHideLabels*>( mMapTools.mShowHideLabels )->layerCanShowHide( vlayer, colShow ) );

    enableMove =
      enableMove ||
      ( qobject_cast<QgsMapToolMoveLabel*>( mMapTools.mMoveLabel ) &&
        ( qobject_cast<QgsMapToolMoveLabel*>( mMapTools.mMoveLabel )->labelMoveable( vlayer, colX, colY )
          || qobject_cast<QgsMapToolMoveLabel*>( mMapTools.mMoveLabel )->diagramMoveable( vlayer, colX, colY ) )
      );

    enableRotate =
      enableRotate ||
      ( qobject_cast<QgsMapToolRotateLabel*>( mMapTools.mRotateLabel ) &&
        qobject_cast<QgsMapToolRotateLabel*>( mMapTools.mRotateLabel )->layerIsRotatable( vlayer, colAng ) );

    enableChange = true;

    if ( enablePin && enableShowHide && enableMove && enableRotate && enableChange )
      break;
  }

  mActionPinLabels->setEnabled( enablePin );
  mActionShowHideLabels->setEnabled( enableShowHide );
  mActionMoveLabel->setEnabled( enableMove );
  mActionRotateLabel->setEnabled( enableRotate );
  mActionChangeLabelProperties->setEnabled( enableChange );

  mMenuPasteAs->setEnabled( clipboard() && !clipboard()->empty() );
  mActionPasteAsNewVector->setEnabled( clipboard() && !clipboard()->empty() );
  mActionPasteAsNewMemoryVector->setEnabled( clipboard() && !clipboard()->empty() );

  updateLayerModifiedActions();

  if ( !layer )
  {
    mActionSelectFeatures->setEnabled( false );
    mActionSelectPolygon->setEnabled( false );
    mActionSelectFreehand->setEnabled( false );
    mActionSelectRadius->setEnabled( false );
    mActionIdentify->setEnabled( QSettings().value( "/Map/identifyMode", 0 ).toInt() != 0 );
    mActionSelectByExpression->setEnabled( false );
    mActionLabeling->setEnabled( false );
    mActionOpenTable->setEnabled( false );
    mActionSelectAll->setEnabled( false );
    mActionInvertSelection->setEnabled( false );
    mActionOpenFieldCalc->setEnabled( false );
    mActionToggleEditing->setEnabled( false );
    mActionToggleEditing->setChecked( false );
    mActionSaveLayerEdits->setEnabled( false );
    mActionSaveLayerDefinition->setEnabled( false );
    mActionLayerSaveAs->setEnabled( false );
    mActionLayerProperties->setEnabled( false );
    mActionLayerSubsetString->setEnabled( false );
    mActionAddToOverview->setEnabled( false );
    mActionFeatureAction->setEnabled( false );
    mActionAddFeature->setEnabled( false );
    mActionCircularStringCurvePoint->setEnabled( false );
    mActionCircularStringRadius->setEnabled( false );
    mActionMoveFeature->setEnabled( false );
    mActionRotateFeature->setEnabled( false );
    mActionOffsetCurve->setEnabled( false );
    mActionNodeTool->setEnabled( false );
    mActionDeleteSelected->setEnabled( false );
    mActionCutFeatures->setEnabled( false );
    mActionCopyFeatures->setEnabled( false );
    mActionPasteFeatures->setEnabled( false );
    mActionCopyStyle->setEnabled( false );
    mActionPasteStyle->setEnabled( false );

    mUndoWidget->dockContents()->setEnabled( false );
    mActionUndo->setEnabled( false );
    mActionRedo->setEnabled( false );
    mActionSimplifyFeature->setEnabled( false );
    mActionAddRing->setEnabled( false );
    mActionFillRing->setEnabled( false );
    mActionAddPart->setEnabled( false );
    mActionDeleteRing->setEnabled( false );
    mActionDeletePart->setEnabled( false );
    mActionReshapeFeatures->setEnabled( false );
    mActionOffsetCurve->setEnabled( false );
    mActionSplitFeatures->setEnabled( false );
    mActionSplitParts->setEnabled( false );
    mActionMergeFeatures->setEnabled( false );
    mActionMergeFeatureAttributes->setEnabled( false );
    mActionRotatePointSymbols->setEnabled( false );
    mActionEnableTracing->setEnabled( false );

    mActionPinLabels->setEnabled( false );
    mActionShowHideLabels->setEnabled( false );
    mActionMoveLabel->setEnabled( false );
    mActionRotateLabel->setEnabled( false );
    mActionChangeLabelProperties->setEnabled( false );

    mActionLocalHistogramStretch->setEnabled( false );
    mActionFullHistogramStretch->setEnabled( false );
    mActionLocalCumulativeCutStretch->setEnabled( false );
    mActionFullCumulativeCutStretch->setEnabled( false );
    mActionIncreaseBrightness->setEnabled( false );
    mActionDecreaseBrightness->setEnabled( false );
    mActionIncreaseContrast->setEnabled( false );
    mActionDecreaseContrast->setEnabled( false );
    mActionZoomActualSize->setEnabled( false );
    mActionZoomToLayer->setEnabled( false );
    return;
  }

  mActionLayerProperties->setEnabled( QgsProject::instance()->layerIsEmbedded(
                                          layer->id() ).isEmpty() );
  mActionAddToOverview->setEnabled( true );
  mActionZoomToLayer->setEnabled( true );

  mActionCopyStyle->setEnabled( true );
  mActionPasteStyle->setEnabled( clipboard()->hasFormat( QGSCLIPBOARD_STYLE_MIME ) );

  /***********Vector layers****************/
  if ( layer->type() == QgsMapLayer::VectorLayer )
  {
    QgsVectorLayer* vlayer = qobject_cast<QgsVectorLayer *>( layer );
    QgsVectorDataProvider* dprovider = vlayer->dataProvider();

    bool isEditable = vlayer->isEditable();
    bool layerHasSelection = vlayer->selectedFeatureCount() > 0;
    bool layerHasActions = vlayer->actions()->size() ||
            !QgsMapLayerActionRegistry::instance()->mapLayerActions( vlayer ).isEmpty();

    mActionLocalHistogramStretch->setEnabled( false );
    mActionFullHistogramStretch->setEnabled( false );
    mActionLocalCumulativeCutStretch->setEnabled( false );
    mActionFullCumulativeCutStretch->setEnabled( false );
    mActionIncreaseBrightness->setEnabled( false );
    mActionDecreaseBrightness->setEnabled( false );
    mActionIncreaseContrast->setEnabled( false );
    mActionDecreaseContrast->setEnabled( false );
    mActionZoomActualSize->setEnabled( false );
    mActionLabeling->setEnabled( true );

    mActionSelectFeatures->setEnabled( true );
    mActionSelectPolygon->setEnabled( true );
    mActionSelectFreehand->setEnabled( true );
    mActionSelectRadius->setEnabled( true );
    mActionIdentify->setEnabled( true );
    mActionSelectByExpression->setEnabled( true );
    mActionOpenTable->setEnabled( true );
    mActionSelectAll->setEnabled( true );
    mActionInvertSelection->setEnabled( true );
    mActionSaveLayerDefinition->setEnabled( true );
    mActionLayerSaveAs->setEnabled( true );
    mActionCopyFeatures->setEnabled( layerHasSelection );
    mActionFeatureAction->setEnabled( layerHasActions );

    if ( !isEditable && mMapCanvas && mMapCanvas->mapTool()
         && mMapCanvas->mapTool()->isEditTool() && !mSaveRollbackInProgress )
    {
      mMapCanvas->setMapTool( mNonEditMapTool );
    }

    if ( dprovider )
    {
      bool canChangeAttributes = dprovider->capabilities() & QgsVectorDataProvider::ChangeAttributeValues;
      bool canDeleteFeatures = dprovider->capabilities() & QgsVectorDataProvider::DeleteFeatures;
      bool canAddFeatures = dprovider->capabilities() & QgsVectorDataProvider::AddFeatures;
      bool canSupportEditing = dprovider->capabilities() & QgsVectorDataProvider::EditingCapabilities;
      bool canChangeGeometry = dprovider->capabilities() & QgsVectorDataProvider::ChangeGeometries;

      mActionLayerSubsetString->setEnabled( !isEditable && dprovider->supportsSubsetString() );

      mActionToggleEditing->setEnabled( canSupportEditing && !vlayer->isReadOnly() );
      mActionToggleEditing->setChecked( canSupportEditing && isEditable );
      mActionSaveLayerEdits->setEnabled( canSupportEditing && isEditable && vlayer->isModified() );
      mUndoWidget->dockContents()->setEnabled( canSupportEditing && isEditable );
      mActionUndo->setEnabled( canSupportEditing );
      mActionRedo->setEnabled( canSupportEditing );

      //start editing/stop editing
      if ( canSupportEditing )
      {
        updateUndoActions();
      }

      mActionPasteFeatures->setEnabled( isEditable && canAddFeatures && !clipboard()->empty() );

      mActionAddFeature->setEnabled( isEditable && canAddFeatures );
      mActionCircularStringCurvePoint->setEnabled( isEditable && ( canAddFeatures || canChangeGeometry )
          && ( vlayer->geometryType() == QGis::Line || vlayer->geometryType() == QGis::Polygon ) );
      mActionCircularStringRadius->setEnabled( isEditable && ( canAddFeatures || canChangeGeometry )
          && ( vlayer->geometryType() == QGis::Line || vlayer->geometryType() == QGis::Polygon ) );


      //does provider allow deleting of features?
      mActionDeleteSelected->setEnabled( isEditable && canDeleteFeatures && layerHasSelection );
      mActionCutFeatures->setEnabled( isEditable && canDeleteFeatures && layerHasSelection );

      //merge tool needs editable layer and provider with the capability of adding and deleting features
      if ( isEditable && canChangeAttributes )
      {
        mActionMergeFeatures->setEnabled( layerHasSelection && canDeleteFeatures && canAddFeatures );
        mActionMergeFeatureAttributes->setEnabled( layerHasSelection );
      }
      else
      {
        mActionMergeFeatures->setEnabled( false );
        mActionMergeFeatureAttributes->setEnabled( false );
      }

      bool isMultiPart = QGis::isMultiType( vlayer->wkbType() ) || !dprovider->doesStrictFeatureTypeCheck();

      // moving enabled if geometry changes are supported
      mActionAddPart->setEnabled( isEditable && canChangeGeometry );
      mActionDeletePart->setEnabled( isEditable && canChangeGeometry );
      mActionMoveFeature->setEnabled( isEditable && canChangeGeometry );
      mActionRotateFeature->setEnabled( isEditable && canChangeGeometry );
      mActionNodeTool->setEnabled( isEditable && canChangeGeometry );

      mActionEnableTracing->setEnabled( isEditable && canAddFeatures &&
                                        ( vlayer->geometryType() == QGis::Line || vlayer->geometryType() == QGis::Polygon ) );

      if ( vlayer->geometryType() == QGis::Point )
      {
        mActionAddFeature->setIcon( NGQgsApplication::getThemeIcon(
                                        "/mActionCapturePoint.svg" ) );

        mActionAddRing->setEnabled( false );
        mActionFillRing->setEnabled( false );
        mActionReshapeFeatures->setEnabled( false );
        mActionSplitFeatures->setEnabled( false );
        mActionSplitParts->setEnabled( false );
        mActionSimplifyFeature->setEnabled( false );
        mActionDeleteRing->setEnabled( false );
        mActionRotatePointSymbols->setEnabled( false );
        mActionOffsetCurve->setEnabled( false );

        if ( isEditable && canChangeAttributes )
        {
          if ( QgsMapToolRotatePointSymbols::layerIsRotatable( vlayer ) )
          {
            mActionRotatePointSymbols->setEnabled( true );
          }
        }
      }
      else if ( vlayer->geometryType() == QGis::Line )
      {
        mActionAddFeature->setIcon( NGQgsApplication::getThemeIcon(
                                        "/mActionCaptureLine.svg" ) );

        mActionReshapeFeatures->setEnabled( isEditable && canChangeGeometry );
        mActionSplitFeatures->setEnabled( isEditable && canAddFeatures );
        mActionSplitParts->setEnabled( isEditable && canChangeGeometry && isMultiPart );
        mActionSimplifyFeature->setEnabled( isEditable && canChangeGeometry );
        mActionOffsetCurve->setEnabled( isEditable && canAddFeatures && canChangeAttributes );

        mActionAddRing->setEnabled( false );
        mActionFillRing->setEnabled( false );
        mActionDeleteRing->setEnabled( false );
      }
      else if ( vlayer->geometryType() == QGis::Polygon )
      {
        mActionAddFeature->setIcon( NGQgsApplication::getThemeIcon(
                                        "/mActionCapturePolygon.svg" ) );

        mActionAddRing->setEnabled( isEditable && canChangeGeometry );
        mActionFillRing->setEnabled( isEditable && canChangeGeometry );
        mActionReshapeFeatures->setEnabled( isEditable && canChangeGeometry );
        mActionSplitFeatures->setEnabled( isEditable && canAddFeatures );
        mActionSplitParts->setEnabled( isEditable && canChangeGeometry && isMultiPart );
        mActionSimplifyFeature->setEnabled( isEditable && canChangeGeometry );
        mActionDeleteRing->setEnabled( isEditable && canChangeGeometry );
        mActionOffsetCurve->setEnabled( false );
      }
      else if ( vlayer->geometryType() == QGis::NoGeometry )
      {
        mActionAddFeature->setIcon( NGQgsApplication::getThemeIcon(
                                        "/mActionNewTableRow.png" ) );
      }

      mActionOpenFieldCalc->setEnabled( true );

      return;
    }
    else
    {
      mUndoWidget->dockContents()->setEnabled( false );
      mActionUndo->setEnabled( false );
      mActionRedo->setEnabled( false );
    }

    mActionLayerSubsetString->setEnabled( false );
  } //end vector layer block
  /*************Raster layers*************/
  else if ( layer->type() == QgsMapLayer::RasterLayer )
  {
    const QgsRasterLayer *rlayer = qobject_cast<const QgsRasterLayer *>( layer );
    if ( rlayer->dataProvider()->dataType( 1 ) != QGis::ARGB32
         && rlayer->dataProvider()->dataType( 1 ) != QGis::ARGB32_Premultiplied )
    {
      if ( rlayer->dataProvider()->capabilities() & QgsRasterDataProvider::Size )
      {
        mActionFullHistogramStretch->setEnabled( true );
      }
      else
      {
        // it would hang up reading the data for WMS for example
        mActionFullHistogramStretch->setEnabled( false );
      }
      mActionLocalHistogramStretch->setEnabled( true );
    }
    else
    {
      mActionLocalHistogramStretch->setEnabled( false );
      mActionFullHistogramStretch->setEnabled( false );
    }

    mActionLocalCumulativeCutStretch->setEnabled( true );
    mActionFullCumulativeCutStretch->setEnabled( true );
    mActionIncreaseBrightness->setEnabled( true );
    mActionDecreaseBrightness->setEnabled( true );
    mActionIncreaseContrast->setEnabled( true );
    mActionDecreaseContrast->setEnabled( true );

    mActionLayerSubsetString->setEnabled( false );
    mActionFeatureAction->setEnabled( false );
    mActionSelectFeatures->setEnabled( false );
    mActionSelectPolygon->setEnabled( false );
    mActionSelectFreehand->setEnabled( false );
    mActionSelectRadius->setEnabled( false );
    mActionZoomActualSize->setEnabled( true );
    mActionOpenTable->setEnabled( false );
    mActionSelectAll->setEnabled( false );
    mActionInvertSelection->setEnabled( false );
    mActionSelectByExpression->setEnabled( false );
    mActionOpenFieldCalc->setEnabled( false );
    mActionToggleEditing->setEnabled( false );
    mActionToggleEditing->setChecked( false );
    mActionSaveLayerEdits->setEnabled( false );
    mUndoWidget->dockContents()->setEnabled( false );
    mActionUndo->setEnabled( false );
    mActionRedo->setEnabled( false );
    mActionSaveLayerDefinition->setEnabled( true );
    mActionLayerSaveAs->setEnabled( true );
    mActionAddFeature->setEnabled( false );
    mActionCircularStringCurvePoint->setEnabled( false );
    mActionCircularStringRadius->setEnabled( false );
    mActionDeleteSelected->setEnabled( false );
    mActionAddRing->setEnabled( false );
    mActionFillRing->setEnabled( false );
    mActionAddPart->setEnabled( false );
    mActionNodeTool->setEnabled( false );
    mActionMoveFeature->setEnabled( false );
    mActionRotateFeature->setEnabled( false );
    mActionOffsetCurve->setEnabled( false );
    mActionCopyFeatures->setEnabled( false );
    mActionCutFeatures->setEnabled( false );
    mActionPasteFeatures->setEnabled( false );
    mActionRotatePointSymbols->setEnabled( false );
    mActionDeletePart->setEnabled( false );
    mActionDeleteRing->setEnabled( false );
    mActionSimplifyFeature->setEnabled( false );
    mActionReshapeFeatures->setEnabled( false );
    mActionSplitFeatures->setEnabled( false );
    mActionSplitParts->setEnabled( false );
    mActionLabeling->setEnabled( false );

    //NOTE: This check does not really add any protection, as it is called on load not on layer select/activate
    //If you load a layer with a provider and idenitfy ability then load another without, the tool would be disabled for both

    //Enable the Identify tool ( GDAL datasets draw without a provider )
    //but turn off if data provider exists and has no Identify capabilities
    mActionIdentify->setEnabled( true );

    QSettings settings;
    int identifyMode = settings.value( "/Map/identifyMode", 0 ).toInt();
    if ( identifyMode == 0 )
    {
      const QgsRasterLayer *rlayer = qobject_cast<const QgsRasterLayer *>( layer );
      const QgsRasterDataProvider* dprovider = rlayer->dataProvider();
      if ( dprovider )
      {
        // does provider allow the identify map tool?
        if ( dprovider->capabilities() & QgsRasterDataProvider::Identify )
        {
          mActionIdentify->setEnabled( true );
        }
        else
        {
          mActionIdentify->setEnabled( false );
        }
      }
    }
  }
}

void NGQgisApp::openURL( QString url, bool useQgisDocDirectory )
{
  // open help in user browser
  if ( useQgisDocDirectory )
  {
    url = "file://" + NGQgsApplication::pkgDataPath() + "/doc/" + url;
  }
#ifdef Q_OS_MACX
  /* Use Mac OS X Launch Services which uses the user's default browser
   * and will just open a new window if that browser is already running.
   * QProcess creates a new browser process for each invocation and expects a
   * commandline application rather than a bundled application.
   */
  CFURLRef urlRef = CFURLCreateWithBytes( kCFAllocatorDefault,
            reinterpret_cast<const UInt8*>( url.toUtf8().data() ), url.length(),
                                          kCFStringEncodingUTF8, nullptr );
  OSStatus status = LSOpenCFURLRef( urlRef, nullptr );
  status = 0; //avoid compiler warning
  CFRelease( urlRef );
#elif defined(Q_OS_WIN)
  if ( url.startsWith( "file://", Qt::CaseInsensitive ) )
    ShellExecute( 0, 0, url.mid( 7 ).toLocal8Bit().constData(), 0, 0, SW_SHOWNORMAL );
  else
    QDesktopServices::openUrl( url );
#else
  QDesktopServices::openUrl( url );
#endif
}

void NGQgisApp::fileNew( bool thePromptToSaveFlag, bool forceBlank )
{
  if ( thePromptToSaveFlag )
  {
    if ( !saveDirty() )
    {
      return; //cancel pressed
    }
  }

  mProjectLastModified = QDateTime();

  QSettings settings;

  closeProject();

  QgsProject* prj = QgsProject::instance();
  prj->clear();

  prj->layerTreeRegistryBridge()->setNewLayersVisible( settings.value(
                                    "/qgis/new_layers_visible", true ).toBool() );

  mLayerTreeCanvasBridge->clear();

  //set the color for selections
  //the default can be set in qgisoptions
  //use project properties to override the color on a per project basis
  int myRed = settings.value( "/qgis/default_selection_color_red", 255 ).toInt();
  int myGreen = settings.value( "/qgis/default_selection_color_green", 255 ).toInt();
  int myBlue = settings.value( "/qgis/default_selection_color_blue", 0 ).toInt();
  int myAlpha = settings.value( "/qgis/default_selection_color_alpha", 255 ).toInt();
  prj->writeEntry( "Gui", "/SelectionColorRedPart", myRed );
  prj->writeEntry( "Gui", "/SelectionColorGreenPart", myGreen );
  prj->writeEntry( "Gui", "/SelectionColorBluePart", myBlue );
  prj->writeEntry( "Gui", "/SelectionColorAlphaPart", myAlpha );
  mMapCanvas->setSelectionColor( QColor( myRed, myGreen, myBlue, myAlpha ) );

  //set the canvas to the default background color
  //the default can be set in qgisoptions
  //use project properties to override the color on a per project basis
  myRed = settings.value( "/qgis/default_canvas_color_red", 255 ).toInt();
  myGreen = settings.value( "/qgis/default_canvas_color_green", 255 ).toInt();
  myBlue = settings.value( "/qgis/default_canvas_color_blue", 255 ).toInt();
  prj->writeEntry( "Gui", "/CanvasColorRedPart", myRed );
  prj->writeEntry( "Gui", "/CanvasColorGreenPart", myGreen );
  prj->writeEntry( "Gui", "/CanvasColorBluePart", myBlue );
  mMapCanvas->setCanvasColor( QColor( myRed, myGreen, myBlue ) );
  mOverviewCanvas->setBackgroundColor( QColor( myRed, myGreen, myBlue ) );

  prj->dirty( false );

  ngSetTitleBarText_( *this );

  //QgsDebugMsg("emiting new project signal");

  // emit signal so listeners know we have a new project
  emit newProject();

  mMapCanvas->freeze( false );
  mMapCanvas->refresh();
  mMapCanvas->clearExtentHistory();
  mMapCanvas->setRotation( 0.0 );
  mScaleEdit->updateScales();

  // set project CRS
  QString defCrs = settings.value( "/Projections/projectDefaultCrs",
                                   GEO_EPSG_CRS_AUTHID ).toString();
  QgsCoordinateReferenceSystem srs;
  srs.createFromOgcWmsCrs( defCrs );
  mMapCanvas->setDestinationCrs( srs );
  // write the projections _proj string_ to project settings
  prj->writeEntry( "SpatialRefSys", "/ProjectCRSProj4String", srs.toProj4() );
  prj->writeEntry( "SpatialRefSys", "/ProjectCrs", srs.authid() );
  prj->writeEntry( "SpatialRefSys", "/ProjectCRSID", static_cast< int >( srs.srsid() ) );
  prj->dirty( false );
  if ( srs.mapUnits() != QGis::UnknownUnit )
  {
    mMapCanvas->setMapUnits( srs.mapUnits() );
  }

  // enable OTF CRS transformation if necessary
  mMapCanvas->setCrsTransformEnabled( settings.value(
                            "/Projections/otfTransformEnabled", 0 ).toBool() );

  updateCRSStatusBar();

  /** New Empty Project Created
      (before attempting to load custom project templates/filepaths) */

  // load default template
  /* NOTE: don't open default template on launch until after initialization,
           in case a project was defined via command line */

  // don't open template if last auto-opening of a project failed
  if ( ! forceBlank )
  {
    forceBlank = ! settings.value( "/qgis/projOpenedOKAtLaunch",
                                   QVariant( true ) ).toBool();
  }

  if ( ! forceBlank && settings.value( "/qgis/newProjectDefault",
                                       QVariant( false ) ).toBool() )
  {
    fileNewFromDefaultTemplate();
  }

  mMapCanvas->setMapTool( mMapTools.mPan );
  mNonEditMapTool = mMapTools.mPan;  // signals are not yet setup to catch this
}

bool NGQgisApp::addProject( const QString& projectFile )
{
  QFileInfo pfi( projectFile );
  statusBar()->showMessage( tr( "Loading project: %1" ).arg( pfi.fileName() ) );
  qApp->processEvents();

  QApplication::setOverrideCursor( Qt::WaitCursor );

  // close the previous opened project if any
  closeProject();

  if ( !QgsProject::instance()->read( projectFile ) )
  {
    QString backupFile = projectFile + "~";
    QString loadBackupPrompt;
    QMessageBox::StandardButtons buttons;
    if ( QFile( backupFile ).exists() )
    {
      loadBackupPrompt = "\n\n" + tr( "Do you want to open the backup file\n%1\ninstead?" ).arg( backupFile );
      buttons |= QMessageBox::Yes;
      buttons |= QMessageBox::No;
    }
    else
    {
      buttons |= QMessageBox::Ok;
    }
    QApplication::restoreOverrideCursor();
    statusBar()->clearMessage();

    int r = QMessageBox::critical( this,
                                   tr( "Unable to open project" ),
                                   QgsProject::instance()->error() + loadBackupPrompt,
                                   buttons );

    if ( QMessageBox::Yes == r && addProject( backupFile ) )
    {
      // We loaded data from the backup file, but we pretend to work on the original project file.
      QgsProject::instance()->setFileName( projectFile );
      QgsProject::instance()->setDirty( true );
      mProjectLastModified = pfi.lastModified();
      return true;
    }

    mMapCanvas->freeze( false );
    mMapCanvas->refresh();
    return false;
  }

  mProjectLastModified = pfi.lastModified();

  ngSetTitleBarText_( *this );
  int  myRedInt = QgsProject::instance()->readNumEntry( "Gui", "/CanvasColorRedPart", 255 );
  int  myGreenInt = QgsProject::instance()->readNumEntry( "Gui", "/CanvasColorGreenPart", 255 );
  int  myBlueInt = QgsProject::instance()->readNumEntry( "Gui", "/CanvasColorBluePart", 255 );
  QColor myColor = QColor( myRedInt, myGreenInt, myBlueInt );
  mMapCanvas->setCanvasColor( myColor ); //this is fill color before rendering starts
  mOverviewCanvas->setBackgroundColor( myColor );

  QgsDebugMsg( "Canvas background color restored..." );
  int myAlphaInt = QgsProject::instance()->readNumEntry( "Gui", "/SelectionColorAlphaPart", 255 );
  myRedInt = QgsProject::instance()->readNumEntry( "Gui", "/SelectionColorRedPart", 255 );
  myGreenInt = QgsProject::instance()->readNumEntry( "Gui", "/SelectionColorGreenPart", 255 );
  myBlueInt = QgsProject::instance()->readNumEntry( "Gui", "/SelectionColorBluePart", 0 );
  myColor = QColor( myRedInt, myGreenInt, myBlueInt, myAlphaInt );
  mMapCanvas->setSelectionColor( myColor ); //this is selection color before rendering starts

  //load project scales
  bool projectScales = QgsProject::instance()->readBoolEntry( "Scales", "/useProjectScales" );
  if ( projectScales )
  {
    mScaleEdit->updateScales( QgsProject::instance()->readListEntry( "Scales", "/ScalesList" ) );
  }

  mMapCanvas->updateScale();
  QgsDebugMsg( "Scale restored..." );

  mActionFilterLegend->setChecked( QgsProject::instance()->readBoolEntry( "Legend", "filterByMap" ) );

  QSettings settings;

  // does the project have any macros?
  if ( mPythonUtils && mPythonUtils->isEnabled() )
  {
    if ( !QgsProject::instance()->readEntry( "Macros", "/pythonCode", QString::null ).isEmpty() )
    {
      int enableMacros = settings.value( "/qgis/enableMacros", 1 ).toInt();
      // 0 = never, 1 = ask, 2 = just for this session, 3 = always

      if ( enableMacros == 3 || enableMacros == 2 )
      {
        enableProjectMacros();
      }
      else if ( enableMacros == 1 ) // ask
      {
        // create the notification widget for macros


        QToolButton *btnEnableMacros = new QToolButton();
        btnEnableMacros->setText( tr( "Enable macros" ) );
        btnEnableMacros->setStyleSheet( "background-color: rgba(255, 255, 255, 0); color: black; text-decoration: underline;" );
        btnEnableMacros->setCursor( Qt::PointingHandCursor );
        btnEnableMacros->setSizePolicy( QSizePolicy::Maximum, QSizePolicy::Preferred );
        connect( btnEnableMacros, SIGNAL( clicked() ), mInfoBar, SLOT( popWidget() ) );
        connect( btnEnableMacros, SIGNAL( clicked() ), this, SLOT( enableProjectMacros() ) );

        QgsMessageBarItem *macroMsg = new QgsMessageBarItem(
          tr( "Security warning" ),
          tr( "project macros have been disabled." ),
          btnEnableMacros,
          QgsMessageBar::WARNING,
          0,
          mInfoBar );
        // display the macros notification widget
        mInfoBar->pushItem( macroMsg );
      }
    }
  }

  emit projectRead(); // let plug-ins know that we've read in a new
  // project so that they can check any project
  // specific plug-in state

  // add this to the list of recently used project files
  saveRecentProjectPath( projectFile, false );

  QApplication::restoreOverrideCursor();

  mMapCanvas->freeze( false );
  mMapCanvas->refresh();

  statusBar()->showMessage( tr( "Project loaded" ), 3000 );
  return true;
}

bool NGQgisApp::fileSave()
{
  QFileInfo fullPath;

  if ( QgsProject::instance()->fileName().isNull() )
  {
    // Retrieve last used project dir from persistent settings
    QSettings settings;
    QString lastUsedDir = settings.value( "/UI/lastProjectDir", QDir::homePath() ).toString();

    QString path = QFileDialog::getSaveFileName(
                     this,
                     tr( "Choose a QGIS project file" ),
                     lastUsedDir + '/' + QgsProject::instance()->title(),
                     tr( "QGIS files" ) + " (*.qgs *.QGS)" );
    if ( path.isEmpty() )
      return false;

    fullPath.setFile( path );

    // make sure we have the .qgs extension in the file name
    if ( "qgs" != fullPath.suffix().toLower() )
    {
      fullPath.setFile( fullPath.filePath() + ".qgs" );
    }


    QgsProject::instance()->setFileName( fullPath.filePath() );
  }
  else
  {
    QFileInfo fi( QgsProject::instance()->fileName() );
    fullPath = fi.absoluteFilePath();
    if ( fi.exists() && !mProjectLastModified.isNull() && mProjectLastModified != fi.lastModified() )
    {
      if ( QMessageBox::warning( this,
                                 tr( "Project file was changed" ),
                                 tr( "The loaded project file on disk was meanwhile changed.  Do you want to overwrite the changes?\n"
                                     "\nLast modification date on load was: %1"
                                     "\nCurrent last modification date is: %2" )
                                 .arg( mProjectLastModified.toString( Qt::DefaultLocaleLongDate ),
                                       fi.lastModified().toString( Qt::DefaultLocaleLongDate ) ),
                                 QMessageBox::Ok | QMessageBox::Cancel ) == QMessageBox::Cancel )
        return false;
    }

    if ( fi.exists() && ! fi.isWritable() )
    {
      messageBar()->pushMessage( tr( "Insufficient permissions" ),
                                 tr( "The project file is not writable." ),
                                 QgsMessageBar::WARNING );
      return false;
    }
  }

  if ( QgsProject::instance()->write() )
  {
    ngSetTitleBarText_( *this ); // update title bar
    statusBar()->showMessage( tr( "Saved project to: %1" ).arg(
                                  QgsProject::instance()->fileName() ), 5000 );

    saveRecentProjectPath( fullPath.filePath() );

    QFileInfo fi( QgsProject::instance()->fileName() );
    mProjectLastModified = fi.lastModified();
  }
  else
  {
    QMessageBox::critical( this,
                           tr( "Unable to save project %1" ).arg(
                               QgsProject::instance()->fileName() ),
                               QgsProject::instance()->error() );
    return false;
  }

  // run the saved project macro
  if ( mTrustedMacros )
  {
    QgsPythonRunner::run( "qgis.utils.saveProjectMacro();" );
  }

  return true;
}

void NGQgisApp::fileSaveAs()
{
  QSettings settings;
  QString lastUsedDir = settings.value( "/UI/lastProjectDir", QDir::homePath() ).toString();

  QString path = QFileDialog::getSaveFileName( this,
                 tr( "Choose a file name to save the QGIS project file as" ),
                 lastUsedDir + '/' + QgsProject::instance()->title(),
                 tr( "QGIS files" ) + " (*.qgs *.QGS)" );
  if ( path.isEmpty() )
    return;

  QFileInfo fullPath( path );

  settings.setValue( "/UI/lastProjectDir", fullPath.path() );

  // make sure the .qgs extension is included in the path name. if not, add it...
  if ( "qgs" != fullPath.suffix().toLower() )
  {
    fullPath.setFile( fullPath.filePath() + ".qgs" );
  }

  QgsProject::instance()->setFileName( fullPath.filePath() );

  if ( QgsProject::instance()->write() )
  {
    ngSetTitleBarText_( *this ); // update title bar
    statusBar()->showMessage( tr( "Saved project to: %1" ).arg(
                                  QgsProject::instance()->fileName() ), 5000 );
    // add this to the list of recently used project files
    saveRecentProjectPath( fullPath.filePath() );
    mProjectLastModified = fullPath.lastModified();
  }
  else
  {
    QMessageBox::critical( this,
                           tr( "Unable to save project %1" ).arg(
                           QgsProject::instance()->fileName() ),
                           QgsProject::instance()->error(),
                           QMessageBox::Ok,
                           Qt::NoButton );
  }
}

void NGQgisApp::projectProperties()
{
  QApplication::setOverrideCursor( Qt::WaitCursor );
  QgsProjectProperties *pp = new QgsProjectProperties( mMapCanvas, this );
  // if called from the status bar, show the projection tab
  if ( mShowProjectionTab )
  {
    pp->showProjectionsTab();
    mShowProjectionTab = false;
  }
  qApp->processEvents();
  // Be told if the mouse display precision may have changed by the user
  // changing things in the project properties dialog box
  connect( pp, SIGNAL( displayPrecisionChanged() ), this,
           SLOT( updateMouseCoordinatePrecision() ) );

  connect( pp, SIGNAL( scalesChanged( const QStringList & ) ), mScaleEdit,
           SLOT( updateScales( const QStringList & ) ) );
  QApplication::restoreOverrideCursor();

  //pass any refresh signals off to canvases
  // Line below was commented out by wonder three years ago (r4949).
  // It is needed to refresh scale bar after changing display units.
  connect( pp, SIGNAL( refresh() ), mMapCanvas, SLOT( refresh() ) );

  // Display the modal dialog box.
  pp->exec();

  qobject_cast<QgsMeasureTool*>( mMapTools.mMeasureDist )->updateSettings();
  qobject_cast<QgsMeasureTool*>( mMapTools.mMeasureArea )->updateSettings();
  qobject_cast<QgsMapToolMeasureAngle*>( mMapTools.mMeasureAngle )->updateSettings();

  // Set the window title.
  ngSetTitleBarText_( *this );

  // delete the property sheet object
  delete pp;
}
