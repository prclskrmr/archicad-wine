/* psgetproc.c: Ersatz fuer powershell.exe im Prefix ~/.wine-bimcloud.
 * Beantwortet die zwei Abfragen, die BIMcloud Manager per PowerShell stellt und auf die Wines powershell.exe nichts ausgibt:
 *   1. Get-Process -Name <Name> | Select-Object -ExpandProperty Id
 *        -> Prozess-IDs, eine je Zeile (AutoPairTimer: Kopplung des lokalen BIMcloud Servers)
 *   2. Get-CimInstance -ClassName Win32_LogicalDisk | Select-Object Caption, FreeSpace, Size
 *        -> Tabelle "Caption FreeSpace Size" je Laufwerk (check-disk-space: Plattenplatz-Anzeige, sonst NoMatchError)
 * Alles andere wird mit unveraenderter Befehlszeile an Wines Original weitergereicht (powershell.exe.wine-orig im selben Ordner).
 * Einsetzen: system32\WindowsPowerShell\v1.0\powershell.exe -> powershell.exe.wine-orig, dann diese .exe als powershell.exe (15-bimcloud.sh ps-an)
 * Bauen: x86_64-w64-mingw32-gcc -O1 -o tools/psgetproc.exe tools/psgetproc.c
 * Test:  WINEPREFIX=~/.wine-bimcloud wine tools/psgetproc.exe -Command "Get-Process -Name explorer | Select-Object -ExpandProperty Id" */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>

static int get_process(const char *cmd)
{
    char name[MAX_PATH] = "";
    const char *p = strstr(cmd, "-Name ");
    if (!p) return -1;
    sscanf(p + 6, "%259[^ |\"]", name);
    strcat(name, ".exe");
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe = { sizeof(pe) };
    int found = 0;
    for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe))
        if (!_stricmp(pe.szExeFile, name)) { printf("%lu\n", pe.th32ProcessID); found++; }
    CloseHandle(snap);
    return found ? 0 : 1;
}

static int logical_disks(void)
{
    DWORD mask = GetLogicalDrives();
    printf("\nCaption FreeSpace     Size\n------- ---------     ----\n");
    for (int i = 0; i < 26; i++)
    {
        char root[4] = { 'A' + i, ':', '\\', 0 };
        ULARGE_INTEGER avail, total, freeb;
        if (!(mask & (1u << i))) continue;
        if (GetDriveTypeA(root) != DRIVE_FIXED) continue;
        if (!GetDiskFreeSpaceExA(root, &avail, &total, &freeb)) continue;
        printf("%c:      %llu %llu\n", 'A' + i, (unsigned long long)freeb.QuadPart, (unsigned long long)total.QuadPart);
    }
    return 0;
}

static int forward(void)
{
    char exe[MAX_PATH], line[32768];
    const char *args = GetCommandLineA();
    /* eigenen Programmnamen ueberspringen */
    if (*args == '"') { args = strchr(args + 1, '"'); args = args ? args + 1 : ""; }
    else while (*args && *args != ' ') args++;
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    strcat(exe, ".wine-orig");
    snprintf(line, sizeof(line), "\"%s\"%s", exe, args);
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(exe, line, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) return 1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD rc = 1;
    GetExitCodeProcess(pi.hProcess, &rc);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return (int)rc;
}

int main(int argc, char **argv)
{
    char cmd[8192] = "";
    for (int i = 1; i < argc; i++) { strncat(cmd, argv[i], sizeof(cmd) - strlen(cmd) - 2); strcat(cmd, " "); }
    if (strstr(cmd, "Get-Process") && strstr(cmd, "-Name ")) return get_process(cmd);
    if (strstr(cmd, "Get-CimInstance") && strstr(cmd, "Win32_LogicalDisk")) return logical_disks();
    return forward();
}
