/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
Connector to let httpd use the vfs filesystem to serve the files in it.
*/
#include <unistd.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "libesphttpd/esp.h"
#include "libesphttpd/httpd.h"

CgiStatus cgiEspVfsDir(HttpdConnData *connData) {
    DIR *pDir=connData->cgiData;
    struct dirent *pDirent;
    int len, i = 0;
    char output[512];	//Temporary buffer for HTML output
	char filename[CONFIG_FATFS_MAX_LFN + 1];
    
    if (connData->isConnectionClosed) {
		//Connection aborted. Clean up.
		return HTTPD_CGI_DONE;
	}

    //First call to this cgi.
	if (pDir==NULL) {
		if (connData->cgiArg != NULL) {
			strncpy(filename, connData->cgiArg, CONFIG_FATFS_MAX_LFN);
            if(filename[strlen(filename) - 1] == '/') filename[strlen(filename) - 1] = '\0';
		} else {
		    strncat(filename, connData->url, CONFIG_FATFS_MAX_LFN - strlen(filename));
        }
		ESP_LOGI(__func__, "GET: %s", filename);
        pDir = opendir(filename);
        if (pDir == NULL) {
            httpdStartResponse(connData, 404);  //http error code 'Not Found'
            httpdEndHeaders(connData);
            return HTTPD_CGI_DONE;
        }
        connData->cgiData=pDir;
        httpdStartResponse(connData, 200); 
        httpdHeader(connData, "Content-Type", "application/json");
        httpdEndHeaders(connData);
        httpdSend(connData, "[ ", -1);
        i=1;
        // return HTTPD_CGI_MORE;
    }

    // switch(connData->requestType) {
        // case HTTPD_METHOD_GET:

            while ((pDirent = readdir(pDir)) != NULL) {
                if(pDirent->d_type==DT_REG) {
                    len=sprintf(output, "%c\"%s\"", (i==0)?',':' ', pDirent->d_name);
                    httpdSend(connData, output, len);
                    return HTTPD_CGI_MORE;
                }
            }

            closedir (pDir);
            httpdSend(connData, " ]", -1);
    //         break;

    //     default:
    //         httpdStartResponse(connData, 405);  //http error code 'Method Not Allowed'
    //         httpdEndHeaders(connData);
    //         // return HTTPD_CGI_DONE;
    // }

    return HTTPD_CGI_DONE;
}
