/////////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 1996-2002 Microsoft Corporation
//
//  Module Name:
//      CluAdmin.cpp
//
//  Abstract:
//      Implementation of the CClusterAdminApp class.
//      Defines the class behaviors for the application.
//
//  Author:
//      David Potter (davidp)   May 1, 1996
//
//  Revision History:
//
//  Notes:
//
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "CluAdmin.h"
#include "ConstDef.h"
#include "CASvc.h"
#include "MainFrm.h"
#include "SplitFrm.h"
#include "ClusDoc.h"
#include "TreeView.h"
#include "OpenClus.h"
#include "ClusMru.h"
#include "ExcOper.h"
#include "Notify.h"
#include "TraceTag.h"
#include "TraceDlg.h"
#include "Barf.h"
#include "BarfDlg.h"
#include "About.h"
#include "CmdLine.h"
#include "VerInfo.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// Global Variables
/////////////////////////////////////////////////////////////////////////////

static LPCTSTR  g_pszProfileName = _T("Cluster Administrator");

#ifdef _DEBUG
CTraceTag   g_tagApp( _T("App"), _T("APP"), 0 );
CTraceTag   g_tagAppMenu( _T("Menu"), _T("APP"), 0 );
CTraceTag   g_tagAppNotify( _T("Notify"), _T("APP NOTIFY"), 0 );
CTraceTag   g_tagNotifyThread( _T("Notify"), _T("NOTIFY THREAD"), 0 );
CTraceTag   g_tagNotifyThreadReg( _T("Notify"), _T("NOTIFY THREAD (REG)"), 0 );
#endif


/////////////////////////////////////////////////////////////////////////////
// CClusterAdminApp
/////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////
// The one and only CClusterAdminApp object

CClusterAdminApp theApp;

IMPLEMENT_DYNAMIC( CClusterNotifyContext, CObject );
IMPLEMENT_DYNAMIC( CClusterAdminApp, CWinApp );

/////////////////////////////////////////////////////////////////////////////
// Message Maps

BEGIN_MESSAGE_MAP( CClusterAdminApp, CWinApp )
    //{{AFX_MSG_MAP(CClusterAdminApp)
    ON_COMMAND(ID_APP_ABOUT, OnAppAbout)
    ON_COMMAND(ID_FILE_OPEN, OnFileOpen)
    ON_COMMAND(ID_FILE_NEW_CLUSTER, OnFileNewCluster)
    ON_COMMAND(ID_WINDOW_CLOSE_ALL, OnWindowCloseAll)
    ON_UPDATE_COMMAND_UI(ID_WINDOW_CLOSE_ALL, OnUpdateWindowCloseAll)
    //}}AFX_MSG_MAP
    // Standard file based document commands
    ON_COMMAND(ID_FILE_NEW, CWinApp::OnFileNew)
#ifdef _DEBUG
    ON_COMMAND(ID_DEBUG_TRACE_SETTINGS, OnTraceSettings)
    ON_COMMAND(ID_DEBUG_BARF_SETTINGS, OnBarfSettings)
    ON_COMMAND(ID_DEBUG_BARF_ALL, OnBarfAllSettings)
#endif // _DEBUG
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterAdminApp::CClusterAdminApp
//
//  Routine Description:
//      Default constructor.
//
//  Arguments:
//      None.
//
//  Return Value:
//      None.
//
//--
/////////////////////////////////////////////////////////////////////////////
CClusterAdminApp::CClusterAdminApp( void )
{
    // TODO: add construction code here,
    // Place all significant initialization in InitInstance
    m_pDocTemplate = NULL;
    m_hchangeNotifyPort = NULL;
    m_lcid = MAKELCID( MAKELANGID( LANG_NEUTRAL, SUBLANG_NEUTRAL ), SORT_DEFAULT );
    m_hOpenedCluster = NULL;
    m_nIdleCount = 0;

    m_punkClusCfgClient = NULL;

    FillMemory( m_rgiimg, sizeof( m_rgiimg ), 0xFF );

} //*** CClusterAdminApp::CClusterAdminApp

/////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterAdminApp::InitInstance
//
//  Routine Description:
//      Initialize this instance of the application.
//
//  Arguments:
//      None.
//
//  Return Value:
//      TRUE        Application successfully initialized.
//      FALSE       Failed to initialize the application.
//
//--
/////////////////////////////////////////////////////////////////////////////
BOOL CClusterAdminApp::InitInstance( void )
{
    BOOL                        bSuccess    = FALSE;
    CMainFrame *                pMainFrame  = NULL;
    CCluAdminCommandLineInfo    cmdInfo;
    HRESULT                     hr;
    size_t                      cch;

    // CG: The following block was added by the Splash Screen component.
    {
//      CCluAdminCommandLineInfo cmdInfo;
//      ParseCommandLine(cmdInfo);
    }

    // Initialize OLE libraries
    if ( ! AfxOleInit() )
    {
        AfxMessageBox( IDP_OLE_INIT_FAILED );
        goto Cleanup;
    }

    if ( CoInitializeSecurity(
                    NULL,
                    -1,
                    NULL,
                    NULL,
                    RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
                    RPC_C_IMP_LEVEL_IMPERSONATE,
                    NULL,
                    EOAC_NONE,
                    0
                    ) != S_OK )
    {
        goto Cleanup;
    } // if:

    // Construct the help path.
    {
        TCHAR   szPath[ _MAX_PATH ];
        TCHAR   szDrive[ _MAX_PATH ];
        TCHAR   szDir[ _MAX_DIR ];
        size_t  cchPath;

        VERIFY( ::GetSystemWindowsDirectory( szPath, _MAX_PATH ) );
        cchPath = _tcslen( szPath );
        if ( szPath[ cchPath - 1 ] != _T('\\') )
        {
            szPath[ cchPath++ ] = _T('\\');
            szPath[ cchPath ] = _T('\0');
        } // if: no backslash on the end of the path
        hr = StringCchCopy( &szPath[ cchPath ], RTL_NUMBER_OF( szPath ) - cchPath, _T("Help\\") );
        ASSERT( SUCCEEDED( hr ) );
        _tsplitpath( szPath, szDrive, szDir, NULL, NULL );
        _tmakepath( szPath, szDrive, szDir, _T("cluadmin"), _T(".hlp") );
        free( (void *) m_pszHelpFilePath );
        BOOL bEnable;
        bEnable = AfxEnableMemoryTracking( FALSE );
        m_pszHelpFilePath = _tcsdup( szPath );
        AfxEnableMemoryTracking( bEnable );
    }  // Construct the help path

    // Standard initialization
    // If you are not using these features and wish to reduce the size
    //  of your final executable, you should remove from the following
    //  the specific initialization routines you do not need.

    SetRegistryKey( IDS_REGKEY_COMPANY );           // Set the registry key for the program.

    //
    // Override the profile name because we don't want to localize it.
    //
    free( (void *) m_pszProfileName );
    cch = _tcslen( g_pszProfileName ) + 1;
    m_pszProfileName = (LPTSTR) malloc( cch * sizeof( *m_pszProfileName ) );
    if ( m_pszProfileName == NULL )
    {
        goto MemoryError;
    } // if: error allocating the profile name buffer
    hr = StringCchCopy( const_cast< LPTSTR >( m_pszProfileName ), cch, g_pszProfileName );
    ASSERT( SUCCEEDED( hr ) );

    InitAllTraceTags();                         // Initialize all trace tags.
    InitBarf();                                 // Initialize Basic Artificial Resource Failure system.

    // Load version information.
#if 0
    {
        CVersionInfo    verinfo;
        DWORD           dwValue;

        // Initialize the version info.
        verinfo.Init();

        // Get the Locale ID.
        if ( verinfo.BQueryValue( _T("\\VarFileInfo\\Translation"), dwValue ) )
        {
            m_lcid = MAKELCID( dwValue, SORT_DEFAULT );
        } // if: locale ID is available
    }  // Load version information
#else
    // Get the locale ID from the system to support MUI.
    m_lcid = GetUserDefaultLCID();
#endif

    // Initialize global CImageList
    InitGlobalImageList();

#ifdef _AFXDLL
    Enable3dControls();             // Call this when using MFC in a shared DLL
#else
    Enable3dControlsStatic();       // Call this when linking to MFC statically
#endif

    LoadStdProfileSettings( 0 );    // Load standard INI file options (including MRU)

    // Create cluster MRU.
    m_pRecentFileList = new CRecentClusterList( 0, _T("Recent Cluster List"), _T("Cluster%d"), 4 );
    if ( m_pRecentFileList == NULL )
    {
        goto MemoryError;
    } // if: error allocating memory
    m_pRecentFileList->ReadList();

    // Register the application's document templates.  Document templates
    //  serve as the connection between documents, frame windows and views.

    m_pDocTemplate = new CMultiDocTemplate(
                        IDR_CLUADMTYPE,
                        RUNTIME_CLASS( CClusterDoc ),
                        RUNTIME_CLASS( CSplitterFrame ), // custom MDI child frame
                        RUNTIME_CLASS( CClusterTreeView )
                        );
    if ( m_pDocTemplate == NULL )
    {
        goto MemoryError;
    } // if: error allocating memory
    AddDocTemplate( m_pDocTemplate );

    // create main MDI Frame window
    pMainFrame = new CMainFrame;
    if ( pMainFrame == NULL )
    {
        goto MemoryError;
    } // if: error allocating memory
    ASSERT( pMainFrame != NULL );
    if ( ! pMainFrame->LoadFrame( IDR_MAINFRAME ) )
    {
        goto Cleanup;
    }  // if:  error loading the frame
    m_pMainWnd = pMainFrame;

    // Parse command line for standard shell commands, DDE, file open
//  cmdInfo.m_nShellCommand = CCommandLineInfo::FileNothing;    // Don't want to do a FileNew.
    ParseCommandLine( cmdInfo );

    // If no commands were specified on the command line, restore the desktop.
    if ( cmdInfo.m_nShellCommand == CCommandLineInfo::FileNothing )
    {
        pMainFrame->PostMessage( WM_CAM_RESTORE_DESKTOP, cmdInfo.m_bReconnect );
    } // if: no commands specified on the command line

    // Create the cluster notification thread.
    if ( ! BInitNotifyThread() )
    {
        goto Cleanup;
    } // if: error creating the cluster notification thread

    // The main window has been initialized, so show and update it.
    {
        WINDOWPLACEMENT wp;

        // Set the placement of the window.
        if ( ReadWindowPlacement( &wp, REGPARAM_SETTINGS, 0 ) )
        {
            pMainFrame->SetWindowPlacement( &wp );
            m_nCmdShow = wp.showCmd; // set the show command.
        }  // if:  read from profile

        // Activate and update the frame window.
        pMainFrame->ActivateFrame( m_nCmdShow );
        pMainFrame->UpdateWindow();
    }  // The main window has been initialized, so show and update it

    // Dispatch commands specified on the command line
    if ( ! ProcessShellCommand( cmdInfo ) )
    {
        goto Cleanup;
    } // if: error processing the command line

    TraceMenu( g_tagAppMenu, AfxGetMainWnd()->GetMenu(), _T("InitInstance menu: ") );

    bSuccess = TRUE;

Cleanup:
    if ( m_pMainWnd != pMainFrame )
    {
        delete pMainFrame;
    } // if: main frame windows allocated but not saved yet
    return bSuccess;

MemoryError:
    CNTException    nte(
                        ERROR_NOT_ENOUGH_MEMORY,
                        0,              // idsOperation
                        NULL,           // pszOperArg1
                        NULL,           // pszOperArg2
                        FALSE           // bAutoDelete
                        );
    nte.ReportError();
    nte.Delete();
    goto Cleanup;

} //*** CClusterAdminApp::InitInstance

/////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterAdminApp::OnIdle
//
//  Routine Description:
//      Process the command line or shell command.
//
//  Arguments:
//      LONG    [IN]    Number of time we have been called before the next
//                      message arrives in the queue
//
//  Return Value:
//      TRUE if more idle processing
//
//--
/////////////////////////////////////////////////////////////////////////////
BOOL CClusterAdminApp::OnIdle(IN LONG lCount)
{
    BOOL bMore = CWinApp::OnIdle(lCount);

    //
    // Since the MFC framework processing many messages lCount was never getting
    // higher than 1.  Since this work should not be done everytime we are idle
    // I added my own counter that determines when the work is done.
    //
    if ((++m_nIdleCount % 200) == 0)
    {
        POSITION        posDoc;                 // position in the documents collection
        POSITION        posDel;                 // position in the to be deleted list
        POSITION        posRemove;              // position in the to be deleted list to remove
        CClusterDoc *   pdoc;
        CClusterItem *  pitem;
        CWaitCursor     cw;

        posDoc = PdocTemplate()->GetFirstDocPosition();
        while (posDoc != NULL)
        {
            pdoc = (CClusterDoc *) PdocTemplate()->GetNextDoc(posDoc);
            ASSERT_VALID(pdoc);
            try
            {
                posDel = pdoc->LpciToBeDeleted().GetHeadPosition();
                while (posDel != NULL)
                {
                    posRemove = posDel;         // save posDel to posRemove since the next call is going to inc posDel
                    pitem = (CClusterItem *) pdoc->LpciToBeDeleted().GetNext(posDel);
                    ASSERT_VALID(pitem);
                    if ((pitem != NULL) && ( pitem->NReferenceCount() == 1))
                    {
                        pdoc->LpciToBeDeleted().RemoveAt(posRemove);    // the saved position posRemove
                    } // if: the list's refence is the only one
                } // while:  more items in the to be deleted list
            }
            catch (CException * pe)
            {
                pe->Delete();
            }  // catch:  CException
        }  // while:  more items in the list

        m_nIdleCount = 0;
        bMore = FALSE;      // don't want any more calls until some new messages are received
    } // if: every 200th time...

    return bMore;

} //*** CClusterAdminApp::OnIdle

/////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterAdminApp::ProcessShellCommand
//
//  Routine Description:
//      Process the command line or shell command.
//
//  Arguments:
//      rCmdInfo    [IN OUT] Command line info.
//
//  Return Value:
//      0           Error.
//      !0          No error.
//
//--
/////////////////////////////////////////////////////////////////////////////
BOOL CClusterAdminApp::ProcessShellCommand(IN OUT CCluAdminCommandLineInfo & rCmdInfo)
{
    BOOL    bSuccess = TRUE;

    if (rCmdInfo.m_nShellCommand == CCommandLineInfo::FileOpen)
    {
        POSITION    pos;

        try
        {
            pos = rCmdInfo.LstrClusters().GetHeadPosition();
            while (pos != NULL)
            {
                OpenDocumentFile(rCmdInfo.LstrClusters().GetNext(pos));
            }  // while:  more clusters in the list
        }  // try
        catch (CException * pe)
        {
            pe->ReportError();
            pe->Delete();
            bSuccess = FALSE;
        }  // catch:  CException
    }  // if:  we are opening clusters
    else
        bSuccess = CWinApp::ProcessShellCommand(rCmdInfo);

    return bSuccess;

} //*** CClusterAdminApp::ProcessShellCommand

/////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterAdminApp::ExitInstance
//
//  Routine Description:
//      Exit this instance of the application.
//
//  Arguments:
//      None.
//
//  Return Value:
//      0       No errors.
//
//--
/////////////////////////////////////////////////////////////////////////////
int CClusterAdminApp::ExitInstance(void)
{
    // Close the notification port.
    if (HchangeNotifyPort() != NULL)
    {
        ::CloseClusterNotifyPort(HchangeNotifyPort());
        m_hchangeNotifyPort = NULL;

        // Allow the notification port threads to clean themselves up.
        ::Sleep(100);
    }  // if:  notification port is open

    // Delete all the items in the notification key list.
    DeleteAllItemData( Cnkl() );
    Cnkl().RemoveAll();

    // Delete all the items in the notification list.
    m_cnlNotifications.RemoveAll();

    CleanupAllTraceTags();                          // Cleanup trace tags.
    CleanupBarf();                                  // Cleanup Basic Artificial Resource Failure system.

    // Release the ClusCfg client object.
    if ( m_punkClusCfgClient != NULL )
    {
        m_punkClusCfgClient->Release();
    }

    return CWinApp::ExitInstance();

} //*** CClusterAdminApp::ExitInstance

/////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterAdminApp::InitGlobalImageList
//
//  Routine Description:
//      Initialize the global image list.
//
//  Arguments:
//      None.
//
//  Return Value:
//      None.
//
//--
/////////////////////////////////////////////////////////////////////////////
void CClusterAdminApp::InitGlobalImageList(void)
{
    // Create small image list.
    VERIFY(PilSmallImages()->Create(
                (int) 16,       // cx
                16,             // cy
                TRUE,           // bMask
                17,             // nInitial
                4               // nGrow
                ));

    PilSmallImages()->SetBkColor(::GetSysColor(COLOR_WINDOW));

    // Load the images into the small image list.
    LoadImageIntoList(PilSmallImages(), IDB_FOLDER_16, IMGLI_FOLDER);
    LoadImageIntoList(PilSmallImages(), IDB_CLUSTER_16, IMGLI_CLUSTER);
    LoadImageIntoList(PilSmallImages(), IDB_CLUSTER_UNKNOWN_16, IMGLI_CLUSTER_UNKNOWN);
    LoadImageIntoList(PilSmallImages(), IDB_NODE_16, IMGLI_NODE);
    LoadImageIntoList(PilSmallImages(), IDB_NODE_DOWN_16, IMGLI_NODE_DOWN);
    LoadImageIntoList(PilSmallImages(), IDB_NODE_PAUSED_16, IMGLI_NODE_PAUSED);
    LoadImageIntoList(PilSmallImages(), IDB_NODE_UNKNOWN_16, IMGLI_NODE_UNKNOWN);
    LoadImageIntoList(PilSmallImages(), IDB_GROUP_16, IMGLI_GROUP);
    LoadImageIntoList(PilSmallImages(), IDB_GROUP_PARTIAL_ONLINE_16, IMGLI_GROUP_PARTIALLY_ONLINE);
    LoadImageIntoList(PilSmallImages(), IDB_GROUP_PENDING_16, IMGLI_GROUP_PENDING);
    LoadImageIntoList(PilSmallImages(), IDB_GROUP_OFFLINE_16, IMGLI_GROUP_OFFLINE);
    LoadImageIntoList(PilSmallImages(), IDB_GROUP_FAILED_16, IMGLI_GROUP_FAILED);
    LoadImageIntoList(PilSmallImages(), IDB_GROUP_UNKNOWN_16, IMGLI_GROUP_UNKNOWN);
    LoadImageIntoList(PilSmallImages(), IDB_RES_16, IMGLI_RES);
    LoadImageIntoList(PilSmallImages(), IDB_RES_OFFLINE_16, IMGLI_RES_OFFLINE);
    LoadImageIntoList(PilSmallImages(), IDB_RES_PENDING_16, IMGLI_RES_PENDING);
    LoadImageIntoList(PilSmallImages(), IDB_RES_FAILED_16, IMGLI_RES_FAILED);
    LoadImageIntoList(PilSmallImages(), IDB_RES_UNKNOWN_16, IMGLI_RES_UNKNOWN);
    LoadImageIntoList(PilSmallImages(), IDB_RESTYPE_16, IMGLI_RESTYPE);
    LoadImageIntoList(PilSmallImages(), IDB_RESTYPE_UNKNOWN_16, IMGLI_RESTYPE_UNKNOWN);
    LoadImageIntoList(PilSmallImages(), IDB_NETWORK_16, IMGLI_NETWORK);
    LoadImageIntoList(PilSmallImages(), IDB_NETWORK_PARTITIONED_16, IMGLI_NETWORK_PARTITIONED);
    LoadImageIntoList(PilSmallImages(), IDB_NETWORK_DOWN_16, IMGLI_NETWORK_DOWN);
    LoadImageIntoList(PilSmallImages(), IDB_NETWORK_UNKNOWN_16, IMGLI_NETWORK_UNKNOWN);
    LoadImageIntoList(PilSmallImages(), IDB_NETIFACE_16, IMGLI_NETIFACE);
    LoadImageIntoList(PilSmallImages(), IDB_NETIFACE_UNREACHABLE_16, IMGLI_NETIFACE_UNREACHABLE);
    LoadImageIntoList(PilSmallImages(), IDB_NETIFACE_FAILED_16, IMGLI_NETIFACE_FAILED);
    LoadImageIntoList(PilSmallImages(), IDB_NETIFACE_UNKNOWN_16, IMGLI_NETIFACE_UNKNOWN);

    // Create large image list.
    VERIFY(PilLargeImages()->Create(
                (int) 32,       // cx
                32,             // cy
                TRUE,           // bMask
                17,             // nInitial
                4               // nGrow
                ));
    PilLargeImages()->SetBkColor(::GetSysColor(COLOR_WINDOW));

    // Load the images into the large image list.
    LoadImageIntoList(PilLargeImages(), IDB_FOLDER_32, IMGLI_FOLDER);
    LoadImageIntoList(PilLargeImages(), IDB_CLUSTER_32, IMGLI_CLUSTER);
    LoadImageIntoList(PilLargeImages(), IDB_CLUSTER_UNKNOWN_32, IMGLI_CLUSTER_UNKNOWN);
    LoadImageIntoList(PilLargeImages(), IDB_NODE_32, IMGLI_NODE);
    LoadImageIntoList(PilLargeImages(), IDB_NODE_DOWN_32, IMGLI_NODE_DOWN);
    LoadImageIntoList(PilLargeImages(), IDB_NODE_PAUSED_32, IMGLI_NODE_PAUSED);
    LoadImageIntoList(PilLargeImages(), IDB_NODE_UNKNOWN_32, IMGLI_NODE_UNKNOWN);
    LoadImageIntoList(PilLargeImages(), IDB_GROUP_32, IMGLI_GROUP);
    LoadImageIntoList(PilLargeImages(), IDB_GROUP_PARTIAL_ONLINE_32, IMGLI_GROUP_PARTIALLY_ONLINE);
    LoadImageIntoList(PilLargeImages(), IDB_GROUP_PENDING_32, IMGLI_GROUP_PENDING);
    LoadImageIntoList(PilLargeImages(), IDB_GROUP_OFFLINE_32, IMGLI_GROUP_OFFLINE);
    LoadImageIntoList(PilLargeImages(), IDB_GROUP_FAILED_32, IMGLI_GROUP_FAILED);
    LoadImageIntoList(PilLargeImages(), IDB_GROUP_UNKNOWN_32, IMGLI_GROUP_UNKNOWN);
    LoadImageIntoList(PilLargeImages(), IDB_RES_32, IMGLI_RES);
    LoadImageIntoList(PilLargeImages(), IDB_RES_OFFLINE_32, IMGLI_RES_OFFLINE);
    LoadImageIntoList(PilLargeImages(), IDB_RES_PENDING_32, IMGLI_RES_PENDING);
    LoadImageIntoList(PilLargeImages(), IDB_RES_FAILED_32, IMGLI_RES_FAILED);
    LoadImageIntoList(PilLargeImages(), IDB_RES_UNKNOWN_32, IMGLI_RES_UNKNOWN);
    LoadImageIntoList(PilLargeImages(), IDB_RESTYPE_32, IMGLI_RESTYPE);
    LoadImageIntoList(PilLargeImages(), IDB_RESTYPE_UNKNOWN_32, IMGLI_RESTYPE_UNKNOWN);
    LoadImageIntoList(PilLargeImages(), IDB_NETWORK_32, IMGLI_NETWORK);
    LoadImageIntoList(PilLargeImages(), IDB_NETWORK_PARTITIONED_32, IMGLI_NETWORK_PARTITIONED);
    LoadImageIntoList(PilLargeImages(), IDB_NETWORK_DOWN_32, IMGLI_NETWORK_DOWN);
    LoadImageIntoList(PilLargeImages(), IDB_NETWORK_UNKNOWN_32, IMGLI_NETWORK_UNKNOWN);
    LoadImageIntoList(PilLargeImages(), IDB_NETIFACE_32, IMGLI_NETIFACE);
    LoadImageIntoList(PilLargeImages(), IDB_NETIFACE_UNREACHABLE_32, IMGLI_NETIFACE_UNREACHABLE);
    LoadImageIntoList(PilLargeImages(), IDB_NETIFACE_FAILED_32, IMGLI_NETIFACE_FAILED);
    LoadImageIntoList(PilLargeImages(), IDB_NETIFACE_UNKNOWN_32, IMGLI_NETIFACE_UNKNOWN);

} //*** CClusterAdminApp::InitGlobalImageList

/////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterAdminApp::LoadImageIntoList
//
//  Routine Description:
//      Load images into an image list.
//
//  Arguments:
//      pil         [IN OUT] Image list into which to load the image.
//      idbImage    [IN] Resource ID for the image bitmap.
//      imgli       [IN] Index into the index array.
//
//  Return Value:
//      None.
//
//--
/////////////////////////////////////////////////////////////////////////////
void CClusterAdminApp::LoadImageIntoList(
    IN OUT CImageList * pil,
    IN ID               idbImage,
    IN UINT             imgli
    )
{
    CBitmap     bm;
    UINT        iimg;

    LoadImageIntoList(pil, idbImage, &iimg);
    if (m_rgiimg[imgli] == (UINT) -1)
        m_rgiimg[imgli] = iimg;
#ifdef DEBUG
    else
        ASSERT(m_rgiimg[imgli] == iimg);
#endif

} //*** CClusterAdminApp::LoadImageIntoList

/////////////////////////////////////////////////////////////////////////////
//++
//
//  static
//  CClusterAdminApp::LoadImageIntoList
//
//  Routine Description:
//      Load images into an image list.
//
//  Arguments:
//      pil         [IN OUT] Image list into which to load the image.
//      idbImage    [IN] Resource ID for the image bitmap.
//      piimg       [OUT] Pointer to image index.
//
//  Return Value:
//      None.
//
//--
/////////////////////////////////////////////////////////////////////////////
void CClusterAdminApp::LoadImageIntoList(
    IN OUT CImageList * pil,
    IN ID               idbImage,
    OUT UINT *          piimg
    )
{
    CBitmap     bm;
    UINT        iimg;
    COLORREF    crMaskColor = RGB(255,0,255);

    ASSERT(pil != NULL);
    ASSERT(idbImage != 0);

    if (piimg == NULL)
        piimg = &iimg;

    bm.LoadBitmap(idbImage);
    *piimg = pil->Add(&bm, crMaskColor);

} //*** CClusterAdminApp::LoadImageIntoList

/////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterAdminApp::OnRestoreDesktop
//
//  Routine Description:
//      Handler for the WM_CAM_RESTORE_DESKTOP message.
//      Restores the desktop from the saved parameters.
//
//  Arguments:
//      wparam      TRUE = reconnect, FALSE, don't reconnect.
//      lparam      Unused.
//
//  Return Value:
//      0
//
//--
/////////////////////////////////////////////////////////////////////////////
LRESULT CClusterAdminApp::OnRestoreDesktop(WPARAM wparam, LPARAM lparam)
{
    CString     strConnections;
    WPARAM      bReconnect = wparam;

    if (bReconnect)
    {
        // Read the connections the user had last time they exited.
        try
        {
            strConnections = GetProfileString(REGPARAM_CONNECTIONS, REGPARAM_CONNECTIONS);
        }  // try
        catch (CException * pe)
        {
            pe->ReportError();
            pe->Delete();
        }  // catch:  CException

        // If there were any connections, restore them.
        if (strConnections.GetLength() > 0)
        {
            LPTSTR          pszConnections;
            LPTSTR          pszConnection;
            TCHAR           szSep[]         = _T(",");

            ASSERT(m_pMainWnd != NULL);

            try
            {
                pszConnections = strConnections.GetBuffer(1);
                pszConnection = _tcstok(pszConnections, szSep);
                while (pszConnection != NULL)
                {
                    // Open a connection to this cluster.
                    OpenDocumentFile(pszConnection);

                    // Find the next connection.
                    pszConnection = _tcstok(NULL, szSep);
                }  // while:  more connections
            }  // try
            catch (CException * pe)
            {
                pe->ReportError();
                pe->Delete();
            } // catch:  CException
            strConnections.ReleaseBuffer();
        }  // if:  connections saved previously
        else
            bReconnect = FALSE;
    }  // if:  reconnect is desired

    if (!bReconnect)
    {
        CWaitCursor wc;
        Sleep(1500);
    }  // if:  not reconnecting

    // If there were no previous connections and we are not minimized, do a standard file open.
    if (!bReconnect && !AfxGetMainWnd()->IsIconic())
        OnFileOpen();

    // Otherwise, restore the desktop.

    return 0;

} //*** CClusterAdminApp::OnRestoreDesktop

/////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterAdminApp::SaveConnections
//
//  Routine Description:
//      Save the current connections so they can be restored later.
//
//  Arguments:
//      None.
//
//  Return Value:
//      None.
//
//--
/////////////////////////////////////////////////////////////////////////////
void CClusterAdminApp::SaveConnections(void)
{
    POSITION        pos;
    CClusterDoc *   pdoc;
    CString         strConnections;
    TCHAR           szSep[]         = _T("\0");

    pos = PdocTemplate()->GetFirstDocPosition();
    while (pos != NULL)
    {
        pdoc = (CClusterDoc *) PdocTemplate()->GetNextDoc(pos);
        ASSERT_VALID(pdoc);
        try
        {
            strConnections += szSep + pdoc->StrNode();
            szSep[0] = _T(',');  // Subsequent connections are preceded by a separator
        }
        catch (CException * pe)
        {
            pe->Delete();
        }  // catch:  CException

        // Save connection-specific settings as well.
        pdoc->SaveSettings();
    }  // while:  more items in the list
    WriteProfileString(REGPARAM_CONNECTIONS, REGPARAM_CONNECTIONS, strConnections);

} //*** CClusterAdminApp::SaveConnections

/////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterAdminApp::OnFileOpen
//
//  Routine Description:
//      Prompt the user for the name of a cluster or server and then open it.
//
//  Arguments:
//      None.
//
//  Return Value:
//      None.
//
//--
/////////////////////////////////////////////////////////////////////////////
void CClusterAdminApp::OnFileOpen(void)
{
    COpenClusterDialog  dlg;
    ID                  idDlgStatus;
    CDocument *         pdoc     = NULL;
    HCLUSTER            hCluster = NULL;
    DWORD               scLastError = 0;
    CString             strClusterName;

    do
    {
        idDlgStatus = (ID) dlg.DoModal();
        if ( idDlgStatus != IDOK )
        {
            break;
        }

        switch ( dlg.m_nAction )
        {
            case OPEN_CLUSTER_DLG_CREATE_NEW_CLUSTER:
                OnFileNewCluster();
                break;

            case OPEN_CLUSTER_DLG_ADD_NODES:
            case OPEN_CLUSTER_DLG_OPEN_CONNECTION:
                if ( hCluster != NULL )
                {
                    CloseCluster( hCluster );
                } // if: previous cluster opened
                hCluster = HOpenCluster( dlg.m_strName );
                if ( hCluster == NULL )
                {
                    scLastError = GetLastError();
                    if( scLastError != ERROR_SUCCESS )
                    {
                        //
                        // GPotts - 6/22/2001 - BUG 410912
                        //
                        // HOpenCluster could return NULL and last error = 0 if GetNodeClusterState
                        // returned either ClusterStateNotInstalled or ClusterStateNotConfigured. 
                        //
                        CNTException    nte( scLastError, IDS_OPEN_CLUSTER_ERROR, dlg.m_strName );
                        nte.ReportError();
                    } // if: last error != 0
                }  // if:  error opening the cluster
                else
                {
                    Trace( g_tagApp, _T("OnFileOpen() - Opening the cluster document on '%s'"), dlg.m_strName );
                    m_hOpenedCluster = hCluster;
                    pdoc = OpenDocumentFile( dlg.m_strName );
                    strClusterName = StrGetClusterName( hCluster );
                    m_hOpenedCluster = NULL;
                    hCluster = NULL;
                }  // else:  cluster opened successfully

                if ( ( pdoc != NULL ) && ( dlg.m_nAction == OPEN_CLUSTER_DLG_ADD_NODES ) )
                {
                    NewNodeWizard(
                        strClusterName,
                        FALSE           // fIgnoreErrors
                        );
                } // if: add a node to the cluster
                break;
        } // switch: dialog action
    }  while ( ( pdoc == NULL )
            && ( dlg.m_nAction != OPEN_CLUSTER_DLG_CREATE_NEW_CLUSTER ) );

    if ( hCluster != NULL )
    {
        CloseCluster( hCluster );
    }

} //*** CClusterAdminApp::OnFileOpen

/////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterAdminApp::OpenDocumentFile
//
//  Routine Description:
//      Open a cluster.
//
//  Arguments:
//      lpszFileName    The name of the cluster or a server in that cluster.
//
//  Return Value:
//      NULL            Invalid cluster or server name.
//      pOpenDocument   The document instance for the open cluster.
//
//--
/////////////////////////////////////////////////////////////////////////////
CDocument * CClusterAdminApp::OpenDocumentFile(LPCTSTR lpszFileName)
{
    // find the highest confidence
    CDocTemplate::Confidence    bestMatch = CDocTemplate::noAttempt;
    CDocTemplate *              pBestTemplate = NULL;
    CDocument *                 pOpenDocument = NULL;

    {
        ASSERT_KINDOF(CDocTemplate, m_pDocTemplate);

        CDocTemplate::Confidence    match;
        ASSERT(pOpenDocument == NULL);
        match = m_pDocTemplate->MatchDocType(lpszFileName, pOpenDocument);
        if (match > bestMatch)
        {
            bestMatch = match;
            pBestTemplate = m_pDocTemplate;
        }
    }

    if (pOpenDocument != NULL)
    {
        POSITION    pos = pOpenDocument->GetFirstViewPosition();
        if (pos != NULL)
        {
            CView *     pView = pOpenDocument->GetNextView(pos); // get first one
            ASSERT_VALID(pView);
            CFrameWnd * pFrame = pView->GetParentFrame();
            if (pFrame != NULL)
                pFrame->ActivateFrame();
            else
                Trace(g_tagApp, _T("Error: Can not find a frame for document to activate."));
            CFrameWnd * pAppFrame;
            if (pFrame != (pAppFrame = (CFrameWnd*)AfxGetApp()->m_pMainWnd))
            {
                ASSERT_KINDOF(CFrameWnd, pAppFrame);
                pAppFrame->ActivateFrame();
            }
        }
        else
        {
            Trace(g_tagApp, _T("Error: Can not find a view for document to activate."));
        }
        return pOpenDocument;
    }

    if (pBestTemplate == NULL)
    {
        TCHAR szMsg[1024];
        AfxLoadString(AFX_IDP_FAILED_TO_OPEN_DOC, szMsg, sizeof(szMsg)/sizeof(szMsg[0]));
        AfxMessageBox(szMsg);
        return NULL;
    }

    return pBestTemplate->OpenDocumentFile(lpszFileName);

} //*** CClusterAdminApp::OpenDocumentFile

/////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterAdminApp::AddToRecentFileList
//
//  Routine Description:
//      Adds a file to the Most Recently Used file list.  Overridden to
//      prevent the cluster name from being fully qualified as a file.
//
//  Arguments:
//      lpszPathName    [IN] The path of the file.
//
//  Return Value:
//      None.
//
//--
/////////////////////////////////////////////////////////////////////////////
void CClusterAdminApp::AddToRecentFileList(LPCTSTR lpszPathName)
{
    ASSERT_VALID(this);
    ASSERT(lpszPathName != NULL);
    ASSERT(AfxIsValidString(lpszPathName));

    if (m_pRecentFileList != NULL)
    {
        // Don't fully qualify the path name.
        m_pRecentFileList->Add(lpszPathName);
    }

} //*** CClusterAdminApp::AddToRecentFileList

/////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterAdminApp::OnFileNewCluster
//
//  Routine Description:
//      Processes the ID_FILE_NEW_CLUSTER menu command.
//
//  Arguments:
//      None.
//
//  Return Value:
//      None.
//
//--
/////////////////////////////////////////////////////////////////////////////
void CClusterAdminApp::OnFileNewCluster( void )
{
    HRESULT                         hr = S_OK;
    IClusCfgCreateClusterWizard *   piWiz;
    VARIANT_BOOL                    fCommitted = VARIANT_FALSE;

    // Get an interface pointer for the wizard.
    hr = CoCreateInstance(
            CLSID_ClusCfgCreateClusterWizard,
            NULL,
            CLSCTX_INPROC_SERVER,
            IID_IClusCfgCreateClusterWizard,
            (LPVOID *) &piWiz
            );
    if ( FAILED( hr ) )
    {
        CNTException nte( hr, IDS_CREATE_CLUSCFGWIZ_OBJ_ERROR, NULL, NULL, FALSE /*bAutoDelete*/ );
        nte.ReportError();
        return;
    }  // if:  error getting the interface pointer

    // Display the wizard.
    hr = piWiz->ShowWizard( HandleToLong( AfxGetMainWnd()->m_hWnd ), &fCommitted );
    if ( FAILED( hr ) )
    {
        CNTException nte( hr, IDS_CREATE_CLUSTER_ERROR, NULL, NULL, FALSE /*bAutoDelete*/ );
        nte.ReportError();
    } // if: error adding cluster nodes

    if ( fCommitted == VARIANT_TRUE )
    {
        BSTR bstrClusterName;
        hr = piWiz->get_ClusterName( &bstrClusterName );
        if ( FAILED( hr ) )
        {
            CNTException nte( hr, IDS_CREATE_CLUSTER_ERROR, NULL, NULL, FALSE /*bAutoDelete*/ );
            nte.ReportError();
        }
        else
        {
            if ( hr == S_OK )
            {
                HCLUSTER hCluster;

                ASSERT( bstrClusterName != NULL );

                // Open the cluster with the cluster name specified by the
                // wizard.  If it not successful, translate this to a NetBIOS
                // name in case that is more reliable.
                hCluster = OpenCluster( bstrClusterName );
                if ( hCluster == NULL )
                {
                    WCHAR   szClusterNetBIOSName[ MAX_COMPUTERNAME_LENGTH + 1 ];
                    DWORD   nSize = sizeof( szClusterNetBIOSName ) / sizeof( szClusterNetBIOSName[ 0 ] );

                    DnsHostnameToComputerName( bstrClusterName, szClusterNetBIOSName, &nSize );
                    SysFreeString( bstrClusterName );
                    bstrClusterName = SysAllocString( szClusterNetBIOSName );
                }
                else
                {
                    CloseCluster( hCluster );
                }
                OpenDocumentFile( bstrClusterName );
            } // if: retrieved cluster name successfully
            SysFreeString( bstrClusterName );
        } // else: retrieving cluster name didn't fail
    } // if: user didn't cancel the wizard

    piWiz->Release();

} //*** CClusterAdminApp::OnFileNewCluster

/////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterAdminApp::OnAppAbout
//
//  Routine Description:
//      Displays the about box.
//
//  Arguments:
//      None.
//
//  Return Value:
//      None.
//
//--
/////////////////////////////////////////////////////////////////////////////
void CClusterAdminApp::OnAppAbout(void)
{
    CAboutDlg aboutDlg;
    aboutDlg.DoModal();

} //*** CClusterAdminApp::OnAppAbout

/////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterAdminApp::OnUpdateWindowCloseAll
//
//  Routine Description:
//      Determines whether menu items corresponding to ID_WINDOW_CLOSE_ALL
//      should be enabled or not.
//
//  Arguments:
//      pCmdUI      [IN OUT] Command routing object.
//
//  Return Value:
//      None.
//
//--
/////////////////////////////////////////////////////////////////////////////
void CClusterAdminApp::OnUpdateWindowCloseAll(CCmdUI * pCmdUI)
{
    pCmdUI->Enable(m_pDocTemplate->GetFirstDocPosition() != NULL);

} //*** CClusterAdminApp::OnUpdateWindowCloseAll

/////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterAdminApp::OnWindowCloseAll
//
//  Routine Description:
//      Processes the ID_WINDOW_CLOSE_ALL menu command.
//
//  Arguments:
//      None.
//
//  Return Value:
//      None.
//
//--
/////////////////////////////////////////////////////////////////////////////
void CClusterAdminApp::OnWindowCloseAll(void)
{
    CloseAllDocuments(FALSE /*bEndSession*/);

} //*** CClusterAdminApp::OnWindowCloseAll

#ifdef _DEBUG
/////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterAdminApp::OnTraceSettings
//
//  Routine Description:
//      Displays the Trace Settings dialog.
//
//  Arguments:
//      None.
//
//  Return Value:
//      None.
//
//--
/////////////////////////////////////////////////////////////////////////////
void CClusterAdminApp::OnTraceSettings(void)
{
    CTraceDialog    dlgTraceSettings;
    dlgTraceSettings.DoModal();

} //*** CClusterAdminApp::OnTraceSettings

/////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterAdminApp::OnBarfSettings
//
//  Routine Description:
//      Displays the BARF Settings dialog.
//
//  Arguments:
//      None.
//
//  Return Value:
//      None.
//
//--
/////////////////////////////////////////////////////////////////////////////
void CClusterAdminApp::OnBarfSettings(void)
{
    DoBarfDialog();

} //*** CClusterAdminApp::OnBarfSettings

/////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterAdminApp::OnBarfAllSettings
//
//  Routine Description:
//      Displays the BARF All Settings dialog.
//
//  Arguments:
//      None.
//
//  Return Value:
//      None.
//
//--
/////////////////////////////////////////////////////////////////////////////
void CClusterAdminApp::OnBarfAllSettings(void)
{
    BarfAll();

} //*** CClusterAdminApp::OnBarfAllSettings

#endif // _DEBUG

/////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterAdminApp::OnClusterNotify
//
//  Routine Description:
//      Handler for the WM_CAM_CLUSTER_NOTIFY message.
//      Processes cluster notifications.
//
//  Arguments:
//      wparam      WPARAM.
//      lparam      LPARAM
//
//  Return Value:
//      Value returned from the application method.
//
//--
/////////////////////////////////////////////////////////////////////////////
LRESULT CClusterAdminApp::OnClusterNotify( WPARAM wparam, LPARAM lparam )
{
    CClusterNotify *    pnotify = NULL;
    BOOL                fHandled;
#if 0
    BOOL                fWindowRepaintsStopped = FALSE;
#endif

    ASSERT( m_cnlNotifications.IsEmpty() == FALSE );

    // Process notifications until the list is empty.
    while ( m_cnlNotifications.IsEmpty() == FALSE )
    {
#if 0
        // If the number of notifications waiting to be processed exceeds
        // a certain threshold, turn off window repaints on the main window.
        if (    ( fWindowRepaintsStopped == FALSE )
            &&  ( m_cnlNotifications.GetCount() > 5 )
            )
        {
            fWindowRepaintsStopped = AfxGetMainWnd()->LockWindowUpdate();
        } // if: too many notifications in the list
#endif

        // Get the next notification.
        pnotify = m_cnlNotifications.Remove();
        ASSERT( pnotify != NULL );
        ASSERT( pnotify->m_dwNotifyKey != 0 );
        if ( pnotify == NULL )
        {
            // This should NEVER happen.
            break;
        } // if: no notification returned

        fHandled = FALSE;

        //
        // If this is a normal notify message, send it to the object
        // that registered it.
        //
        if ( pnotify->m_emt == CClusterNotify::EMessageType::mtNotify )
        {

            // Send change notifications to the object that registered it.
            if ( pnotify->m_pcnk != NULL )
            {
                // Find the notification key in our list of keys.  If it is not
                // found, ignore it.  Otherwise, ask the object that registered
                // the notification to handle it.
                if ( Cnkl().Find( pnotify->m_pcnk ) != NULL )
                {
                    switch ( pnotify->m_pcnk->m_cnkt )
                    {
                        case cnktDoc:
                            ASSERT_VALID( pnotify->m_pcnk->m_pdoc );
                            pnotify->m_pcnk->m_pdoc->OnClusterNotify( pnotify );
                            pnotify = NULL;
                            break;

                        case cnktClusterItem:
                            ASSERT_VALID( pnotify->m_pcnk->m_pci );
                            ASSERT_VALID( pnotify->m_pcnk->m_pci->Pdoc() );
                            pnotify->m_pcnk->m_pci->OnClusterNotify( pnotify );
                            pnotify = NULL;
                            break;
                    }  // switch:  notification key type
                }  // if:  notification key found in the list
            } // if: non-NULL object pointer

            // Notification not handled.
            if ( fHandled == FALSE )
            {
                Trace( g_tagError, _T("*** Unhandled notification: key %08.8x, filter %x (%s) - '%s'"), pnotify->m_dwNotifyKey, pnotify->m_dwFilterType, PszNotificationName( pnotify->m_dwFilterType ), pnotify->m_strName );
            }
        } // if: normal notify message
        else if ( pnotify->m_emt == CClusterNotify::EMessageType::mtRefresh )
        {
            //
            // This is a refresh notify message.  Refresh all connections.
            //
            POSITION        pos;
            CClusterDoc *   pdoc;

            pos = PdocTemplate()->GetFirstDocPosition();
            while ( pos != NULL )
            {
                pdoc = (CClusterDoc *) PdocTemplate()->GetNextDoc(pos);
                ASSERT_VALID(pdoc);
                try
                {
                    pdoc->OnCmdRefresh();
                }
                catch ( CException * pe )
                {
                    pe->Delete();
                }  // catch:  CException
            } // while: more documents in the list
        } // else if: refresh notify message

        delete pnotify;
        pnotify = NULL;

    } // while: more notifications

#if 0
    // If we stopped window repaints, turn them on again.
    if ( fWindowRepaintsStopped )
    {
        AfxGetMainWnd()->UnlockWindowUpdate();
    } // if: we stopped window repaints
#endif

    delete pnotify;
    return 0;

} //*** CClusterAdminApp::OnClusterNotify

/////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterAdminApp::BInitNotifyThread
//
//  Routine Description:
//      Initialize the cluster notification thread.
//
//  Arguments:
//      None.
//
//  Return Value:
//      TRUE        Thread initialized successfully.
//      FALSE       Thread NOT initialized successfully.
//
//--
/////////////////////////////////////////////////////////////////////////////
BOOL CClusterAdminApp::BInitNotifyThread(void)
{
    try
    {
        // Create the notification port.
        m_hchangeNotifyPort = ::CreateClusterNotifyPort(
                                    (HCHANGE) INVALID_HANDLE_VALUE,     // hChange
                                    (HCLUSTER) INVALID_HANDLE_VALUE,    // hCluster
                                    0,                                  // dwFilter
                                    0                                   // dwNotifyKey
                                    );
        if (HchangeNotifyPort() == NULL)
        {
            ThrowStaticException(GetLastError());
        }

        // Construct the context object.
        Pcnctx()->m_hchangeNotifyPort = HchangeNotifyPort();
        Pcnctx()->m_hwndFrame = m_pMainWnd->m_hWnd;
        Pcnctx()->m_pcnlList = &Cnl();

        // Begin the thread.
        m_wtNotifyThread = AfxBeginThread(NotifyThreadProc, Pcnctx());
        if (WtNotifyThread() == NULL)
        {
            ThrowStaticException(GetLastError());
        }
    }  // try
    catch (CException * pe)
    {
        // Close the notify port.
        if (HchangeNotifyPort() != NULL)
        {
            ::CloseClusterNotifyPort(HchangeNotifyPort());
            m_hchangeNotifyPort = NULL;
        }  // if:  notify port is open

        pe->ReportError();
        pe->Delete();
        return FALSE;
    }  // catch:  CException

    return TRUE;

} //*** CClusterAdminApp::BInitNotifyThread

/////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterAdminApp::NotifyThreadProc (static)
//
//  Routine Description:
//      Notification thread procedure.
//
//  Arguments:
//      pParam      [IN OUT] Thread procedure parameter -- a notification
//                    context object.
//
//  Return Value:
//      None.
//
//--
/////////////////////////////////////////////////////////////////////////////
UINT AFX_CDECL CClusterAdminApp::NotifyThreadProc(LPVOID pParam)
{
    DWORD                           dwStatus;
    WCHAR*                          pwszName;
    DWORD                           cchName;
    DWORD                           cchBuffer;
    DWORD_PTR                       dwNotifyKey;
    DWORD                           dwFilterType;
    DWORD                           nTimeout;
    BOOL                            fQueueIsFull = FALSE;
    CClusterNotify *                pnotify = NULL;
    CClusterNotifyContext *         pnctx   = (CClusterNotifyContext *) pParam;
    CClusterNotify::EMessageType    emt = CClusterNotify::EMessageType::mtNotify;

    ASSERT( pParam != NULL );
    ASSERT_KINDOF( CClusterNotifyContext, pnctx );
    ASSERT( pnctx->m_hchangeNotifyPort != NULL );
    ASSERT( pnctx->m_hwndFrame != NULL );

    //
    // Allocate a buffer that should be large enough for most notifications.
    // If it isn't, it will be reallocated in the loop below.
    //
    cchBuffer = 1024;
    pwszName = new WCHAR[ 1024 ];
    ASSERT( pwszName != NULL );
    if ( pwszName == NULL )
    {
        AfxThrowMemoryException();
    } // if: memory exception

    //
    // Default to waiting an infinite amount of time for the next notification
    // to be delivered.  We will change this if we ever get too many
    // notifications in the queue.
    //
    nTimeout = INFINITE;

    for (;;)
    {
        //
        // Get the next notification.
        // Wait for one if there isn't one waiting for us.
        //
        cchName = cchBuffer;
        dwStatus = GetClusterNotify(
                        pnctx->m_hchangeNotifyPort,
                        &dwNotifyKey,
                        &dwFilterType,
                        pwszName,
                        &cchName,
                        nTimeout
                        );
                        
        if ( dwStatus == ERROR_INVALID_HANDLE )
        {
            //
            // The notification port was closed.

            break;
        } // if: invalid handle error

        if ( dwStatus == ERROR_MORE_DATA )
        {
            //
            // The name buffer was too small.
            // Allocate a new one.
            //

            cchName++;              // Add one for NULL
            
            ASSERT( cchName > cchBuffer ); 
            
            cchBuffer = cchName;

            // Reallocate the name buffer.
            delete [] pwszName;
            pwszName = new WCHAR[ cchBuffer ];
            ASSERT( pwszName != NULL );
            if ( pwszName == NULL )
            {
                AfxThrowMemoryException();
            } // if: memory exception

            // Loop around and try again.
            continue;
        } // if: buffer too small

        if ( dwStatus == WAIT_TIMEOUT )
        {
            //
            // The call to GetClusterNotify timed out.  This will only happen
            // if we detected that there were too many notifications in the
            // queue and stopped saving them.  Send a refresh message to the
            // main window and reset the timeout to INFINITE so that
            // we will wait until there is another event to get.
            //
            nTimeout = INFINITE;
            emt = CClusterNotify::EMessageType::mtRefresh;
            fQueueIsFull = FALSE;
        } // if: GetClusterNotify timed out
        else if ( dwStatus != ERROR_SUCCESS )
        {
            // Some other failure occurred getting the notification.
            TraceError(_T("CClusterAdminApp::NotifyThreadProc() %s"), dwStatus);
            continue;
        }  // else if: error getting notification

        //
        // If we have exceeded the max queue size threshold, don't send this
        // notification to the main UI thread.  Instead change the timeout
        // value so that we will just keep getting notifications off the
        // queue until there aren't anymore.  Once that has happened,
        // we will send a refresh event to the main UI thread and it will
        // refresh all the connections.
        //
        if (    ( emt == CClusterNotify::EMessageType::mtNotify )
            &&  ( pnctx->m_pcnlList->GetCount() > 500 )
            )
        {
            nTimeout = 2000;
            fQueueIsFull = TRUE;
        } // if: queue is full

        if ( fQueueIsFull == FALSE )
        {
            //
            // Package the notification info up to send to the main UI thread.
            //

            try
            {
                // Allocate the notification object and initialize it.
                pnotify = new CClusterNotify( emt, dwNotifyKey, dwFilterType, pwszName );
                ASSERT( pnotify != NULL );
                if ( pnotify == NULL )
                {
                    // Failed to allocate, so ignore this notification.
                    continue;
                } // if: error allocating and initialize the notify object

#ifdef _DEBUG
                if ( emt == CClusterNotify::EMessageType::mtNotify )
                {
                    TCHAR *     pszTracePrefix;
                    CTraceTag * ptag;

                    pszTracePrefix = _T("");
                    if (   ( dwNotifyKey == NULL )
                        || ( dwNotifyKey == 0xfeeefeee )
                        || ( dwNotifyKey == 0xbaadf00d ) )
                    {
                        ptag = &g_tagError;
                        pszTracePrefix = _T("*** NOTIFY THREAD ");
                    }  // if:  bad notification key
                    else if ( dwFilterType & (CLUSTER_CHANGE_REGISTRY_NAME | CLUSTER_CHANGE_REGISTRY_ATTRIBUTES | CLUSTER_CHANGE_REGISTRY_VALUE) )
                    {
                        ptag = &g_tagNotifyThreadReg;
                    }
                    else
                    {
                        ptag = &g_tagNotifyThread;
                    }
                    Trace( *ptag, _T("%sNotification - key %08.8x, filter %x (%s), %s"), pszTracePrefix, dwNotifyKey, dwFilterType, PszNotificationName(dwFilterType), pnotify->m_strName );
                } // if: normal notification
#endif

                // Add the item to the list.
                // The pointer is NULL upon return.
                pnctx->m_pcnlList->Add( &pnotify );

                // Release the list lock.

                // Post a message to the main window to tell the main thread
                // there is new information in the list.
                if ( ! ::PostMessage( pnctx->m_hwndFrame, WM_CAM_CLUSTER_NOTIFY, NULL, NULL ) )
                {
                } // if: PostMessage failed

                emt = CClusterNotify::EMessageType::mtNotify;
                fQueueIsFull = FALSE;

            } // try
            catch ( ... )
            {
                if ( pnotify != NULL )
                {
                    delete pnotify;
                    pnotify = NULL;
                } // if: notification record allocated
            } // catch: any exception
        } // if: notification queue is not full
    }  // forever: get notifications until the notification port is closed

    delete [] pwszName;
    delete pnotify;

    return 0;

} //*** CClusterAdminApp::NotifyThreadProc


//*************************************************************************//


/////////////////////////////////////////////////////////////////////////////
// Global Functions
/////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////
//++
//
//  BCreateFont
//
//  Routine Description:
//      Create a font.
//
//  Arguments:
//      rfont       [OUT] Font to create.
//      nPoints     [IN] Point size.
//      bBold       [IN] Flag specifying whether font is bold or not.
//
//  Return Value:
//      TRUE        Font created successfully.
//      FALSE       Error creating font.
//
//--
/////////////////////////////////////////////////////////////////////////////
BOOL BCreateFont(OUT CFont & rfont, IN int nPoints, IN BOOL bBold)
{
    return rfont.CreateFont(
                    -nPoints,                           // nHeight
                    0,                                  // nWidth
                    0,                                  // nEscapement
                    0,                                  // nOrientation
                    (bBold ? FW_BOLD : FW_DONTCARE),    // nWeight
                    FALSE,                              // bItalic
                    FALSE,                              // bUnderline
                    FALSE,                              // cStrikeout
                    ANSI_CHARSET,                       // nCharSet
                    OUT_DEFAULT_PRECIS,                 // nOutPrecision
                    CLIP_DEFAULT_PRECIS,                // nClipPrecision
                    DEFAULT_QUALITY,                    // nQuality
                    DEFAULT_PITCH | FF_DONTCARE,        // nPitchAndFamily
                    _T("MS Shell Dlg")                  // lpszFaceName
                    );

} //*** BCreateFont

/////////////////////////////////////////////////////////////////////////////
//++
//
//  NewNodeWizard
//
//  Routine Description:
//      Invoke the Add Nodes to Cluster Wizard.
//
//  Arguments:
//      pcszName        -- Name of cluster to add nodes to.
//      fIgnoreErrors   -- TRUE = don't display error messages.
//                          Defaults to FALSE.
//
//  Return Value:
//      None.
//
//--
/////////////////////////////////////////////////////////////////////////////
void NewNodeWizard(
    LPCTSTR pcszName,
    BOOL    fIgnoreErrors   // = FALSE
    )
{
    HRESULT                     hr = S_OK;
    IClusCfgAddNodesWizard *    piWiz = NULL;
    BSTR                        bstrConnectName = NULL;
    VARIANT_BOOL                fCommitted = VARIANT_FALSE;

    // Get an interface pointer for the wizard.
    hr = CoCreateInstance(
            CLSID_ClusCfgAddNodesWizard,
            NULL,
            CLSCTX_INPROC_SERVER,
            IID_IClusCfgAddNodesWizard,
            (void **) &piWiz
            );
    if ( FAILED( hr ) )
    {
        if ( ! fIgnoreErrors )
        {
            CNTException nte( hr, IDS_CREATE_CLUSCFGWIZ_OBJ_ERROR, NULL, NULL, FALSE /*bAutoDelete*/ );
            nte.ReportError();
        }
        return;
    } // if: error getting the interface pointer

    // Specify the name of the cluster we are going to add a node to.
    bstrConnectName = SysAllocString( pcszName );
    if ( bstrConnectName == NULL )
    {
        AfxThrowMemoryException();
    }
    hr = piWiz->put_ClusterName( bstrConnectName );
    if ( FAILED( hr ) )
    {
        if ( ! fIgnoreErrors )
        {
            CNTException nte( hr, IDS_ADD_NODES_TO_CLUSTER_ERROR, bstrConnectName, NULL, FALSE /*bAutoDelete*/ );
            nte.ReportError();
        }
    } // if: error setting the cluster name

    // Display the wizard.
    hr = piWiz->ShowWizard( HandleToLong( AfxGetMainWnd()->m_hWnd ), &fCommitted );
    if ( FAILED( hr ) )
    {
        if ( ! fIgnoreErrors )
        {
            CNTException nte( hr, IDS_ADD_NODES_TO_CLUSTER_ERROR, bstrConnectName, NULL, FALSE /*bAutoDelete*/ );
            nte.ReportError();
        }
    } // if: error adding cluster nodes

    SysFreeString( bstrConnectName );
    piWiz->Release();

}  //*** NewNodeWizard


/////////////////////////////////////////////////////////////////////////////
//++
//
//  GetClusterInformation
//
//  Routine Description:
//      Given a cluster handle, retrieve the cluster's hostname label and
//      version information.
//
//  Arguments:
//      hClusterIn
//          Handle to the cluster; must not be null.
//
//      rstrNameOut
//          On return, the cluster's hostname label.
//
//      pcviOut
//          Address for cluster's version info on return; can be null if 
//          the caller doesn't care.
//
//  Return Value:
//      None.
//
//  Remarks:
//      Throws an exception on failure.
//
//--
/////////////////////////////////////////////////////////////////////////////
void GetClusterInformation( HCLUSTER hClusterIn, CString& rstrNameOut, PCLUSTERVERSIONINFO pcviOut )
{
    DWORD       cchName = MAX_CLUSTERNAME_LENGTH; // Just a guess for the first try.
    DWORD       cchNameStash = cchName;
    CString     strClusterName;
    DWORD       scClusterInfo = ERROR_SUCCESS;

    ASSERT( hClusterIn != NULL );
    if ( pcviOut != NULL )
    {
        pcviOut->dwVersionInfoSize = sizeof( *pcviOut );
    }

    scClusterInfo = GetClusterInformation( hClusterIn, strClusterName.GetBuffer( cchName ), &cchName, pcviOut );
    strClusterName.ReleaseBuffer( cchNameStash );
    if ( scClusterInfo == ERROR_MORE_DATA )
    {
        cchNameStash = ++cchName; // KLUDGE: ++ because GetClusterInformation is STOOPID.
        scClusterInfo = GetClusterInformation( hClusterIn, strClusterName.GetBuffer( cchName ), &cchName, pcviOut );
        strClusterName.ReleaseBuffer( cchNameStash );
    }

    if ( scClusterInfo != ERROR_SUCCESS )
    {
        ThrowStaticException( scClusterInfo );
    }

    rstrNameOut = strClusterName;

} //*** GetClusterInformation

/////////////////////////////////////////////////////////////////////////////
//++
//
//  StrGetClusterName
//
//  Routine Description:
//      Given a cluster handle, retrieve the cluster's FQDN if possible,
//      or its IP address if not.
//
//  Arguments:
//      hClusterIn
//          Handle to the cluster; must not be null.
//
//  Return Value:
//      The cluster's FQDN or its IP address.
//
//  Remarks:
//      Throws an exception on failure.  To retrieve just the cluster's
//      hostname label, use GetClusterInformation
//
//--
/////////////////////////////////////////////////////////////////////////////
CString StrGetClusterName( HCLUSTER hClusterIn )
{
    CString strClusterName;
    DWORD   cchName = DNS_MAX_NAME_LENGTH;
    DWORD   sc = ERROR_SUCCESS;

    ASSERT( hClusterIn != NULL );

    //
    //  First, try to get the FQDN.
    //
    {
        DWORD cbFQDN = cchName * sizeof( TCHAR );
        DWORD cbBytesRequired = 0;
        sc = ClusterControl(
                hClusterIn,
                NULL,
                CLUSCTL_CLUSTER_GET_FQDN,
                NULL,
                NULL,
                strClusterName.GetBuffer( cchName ),
                cbFQDN,
                &cbBytesRequired
                );
        strClusterName.ReleaseBuffer( cchName );
        if ( sc == ERROR_MORE_DATA )
        {
            cchName = ( cbBytesRequired / sizeof( TCHAR ) ) + 1;
            cbFQDN = cchName * sizeof( TCHAR );
            sc = ClusterControl(
                    hClusterIn,
                    NULL,
                    CLUSCTL_CLUSTER_GET_FQDN,
                    NULL,
                    NULL,
                    strClusterName.GetBuffer( cchName ),
                    cbFQDN,
                    &cbBytesRequired
                    );
            strClusterName.ReleaseBuffer( cchName );
        }
    }

    //
    //  If ClusterControl returned ERROR_INVALID_FUNCTION, it's probably Win2k, so
    //  make do with just the hostname label; if it failed for some other reason,
    //  try to get the IP address.
    //
    if ( sc == ERROR_INVALID_FUNCTION )
    {
        GetClusterInformation( hClusterIn, strClusterName, NULL );
    }
    else if ( sc != ERROR_SUCCESS )
    {
        HRESOURCE hIPResource = NULL;
        CClusPropList           cpl;
        CLUSPROP_BUFFER_HELPER  cpbh;

        sc = ResUtilGetCoreClusterResources( hClusterIn, NULL, &hIPResource, NULL );
        if ( sc != ERROR_SUCCESS )
            ThrowStaticException( sc );

        sc = cpl.ScGetResourceProperties( hIPResource, CLUSCTL_RESOURCE_GET_PRIVATE_PROPERTIES );
        if ( sc != ERROR_SUCCESS )
            ThrowStaticException( sc );

        sc = cpl.ScMoveToPropertyByName( L"Address" );
        if ( sc != ERROR_SUCCESS )
            ThrowStaticException( sc );

        cpbh = cpl.CbhCurrentValue();
        ASSERT( cpbh.pSyntax->dw == CLUSPROP_SYNTAX_LIST_VALUE_SZ );

        strClusterName = cpbh.pStringValue->sz;
    }

    return strClusterName;

} //*** StrGetClusterName
