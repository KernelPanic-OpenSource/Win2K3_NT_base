
//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
#include "regedt32.h"

// stolen from the CRT, used to shrink our code

int _stdcall ModuleEntry(void)
{
    STARTUPINFO si;
    LPSTR pszCmdLine = GetCommandLine();

    if ( *pszCmdLine == '\"' ) {
        /*
         * Scan, and skip over, subsequent characters until
         * another double-quote or a null is encountered.
         */
        while ( *++pszCmdLine && (*pszCmdLine
             != '\"') );
        /*
         * If we stopped on a double-quote (usual case), skip
         * over it.
         */
        if ( *pszCmdLine == '\"' )
            pszCmdLine++;
    }
    else {
        while (*pszCmdLine > ' ')
            pszCmdLine++;
    }

    /*
     * Skip past any white space preceeding the second token.
     */
    while (*pszCmdLine && (*pszCmdLine <= ' ')) {
        pszCmdLine++;
    }

    si.dwFlags = 0;
    GetStartupInfoA(&si);

    return WinMain(GetModuleHandle(NULL), NULL, pszCmdLine,
                   si.dwFlags & STARTF_USESHOWWINDOW ? si.wShowWindow : SW_SHOWDEFAULT);

}


const char szFile[] = "regedit.exe";

//---------------------------------------------------------------------------
int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    TCHAR szFullPath[MAX_PATH+1];

    *szFullPath = 0;
    if (GetWindowsDirectory(szFullPath, ARRAYSIZE(szFullPath)))
    {
        if (!PathAppend(szFullPath, szFile))
        {
            *szFullPath = 0;
        }
        szFullPath[MAX_PATH] = 0;           // Ensure NULL termination
    }

    if (!*szFullPath)
    {
        StringCchCopy(szFullPath, ARRAYSIZE(szFullPath), szFile);
    }

    ShellExecute(HWND_DESKTOP, NULL, szFullPath, lpCmdLine, NULL, nCmdShow);
    ExitProcess(0);
    return 0;
}
