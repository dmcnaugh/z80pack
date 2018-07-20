/**
 * esp32_diskmanager.c
 * 
 */

#include <unistd.h>
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "libesphttpd/esp.h"
#include "libesphttpd/httpd.h"

#include "sim.h"

char *disks[4] = { NULL, NULL, NULL, NULL };
// = {
// 	"drivea.dsk",
// 	"driveb.dsk",
// 	"drivec.dsk",
// 	"drived.dsk"
// };
extern char *disks[];

char fn[CONFIG_FATFS_MAX_LFN];		/* path/filename for disk image */
extern char fn[];

void readDiskmap(void);
void writeDiskmap(void);

static void sendDisks(HttpdConnData *connData) {
    int i, len;
    char output[512];	//Temporary buffer for HTML output
    
    httpdStartResponse(connData, 200); 
    httpdHeader(connData, "Content-Type", "application/json");
    httpdEndHeaders(connData);

    httpdSend(connData, "{", -1);

    for(i=0; i<4; i++) {
        len=sprintf(output, "\"%c\": \"%s\"", i+'A', disks[i]==NULL?"":disks[i]);
        httpdSend(connData, output, len);
        if(i<3) httpdSend(connData, ",", -1);
    }

    httpdSend(connData, "}", -1);          
}

static int getDiskID(HttpdConnData *connData) {
    int disk = -1;
    if(connData->getArgs!=NULL) {
        disk = connData->getArgs[0] - 'A';
        if(disk < 0 || disk > 3) {
            ESP_LOGW(__func__, "BAD DISK ID: %c", connData->getArgs[0]);   
            httpdStartResponse(connData, 404);  //http error code 'Not Found'
            httpdEndHeaders(connData);    
            disk = -1;
        }
    } else {
        ESP_LOGW(__func__, "DISK ID REQUIRED");   
        httpdStartResponse(connData, 404);  //http error code 'Not Found'
        httpdEndHeaders(connData);       
    }
    return disk;
}

CgiStatus   cgiDisks(HttpdConnData *connData) {
    int disk, i;
    char *image = NULL;
 
    if (connData->isConnectionClosed) {
		//Connection aborted. Clean up.
		return HTTPD_CGI_DONE;
	}

    switch(connData->requestType) {
        case HTTPD_METHOD_GET:
            ESP_LOGI(__func__, "GET /disks: %s", connData->getArgs==NULL?"":connData->getArgs);
            sendDisks(connData);
            // return HTTPD_CGI_DONE;
            break;
        case HTTPD_METHOD_PUT:
            ESP_LOGI(__func__, "PUT /disks: %s", connData->getArgs==NULL?"":connData->getArgs);
            disk = getDiskID(connData);
            if(disk != -1) {
                //disk = ARG
                //imageName = ARG
                if(disks[disk] != NULL) {
                    ESP_LOGW(__func__, "PUT /disks NOT EMPTY");   
                    httpdStartResponse(connData, 404);  //http error code 'Not Found'
                    httpdEndHeaders(connData); 
                    return HTTPD_CGI_DONE;     
                }

                if(connData->post.len > 0) {
                    ESP_LOGI(__func__, "POST(PUT) length: %d, %s", connData->post.len, connData->post.buff);   
                    image = malloc(connData->post.len + 1);
                    //todo: erroc check the malloc
                    strcpy(image, connData->post.buff);
                    /* check that file is not already in disks[] */
                    for(i=0; i<4; i++) {
                        if(disks[i] != NULL) {
                            if(strcmp(image, disks[i]) == 0) {
                                ESP_LOGW(__func__, "PUT image: %s, already inserted in disks", image);   
                                httpdStartResponse(connData, 404);  //http error code 'Not Found'
                                httpdEndHeaders(connData);   
                                free(image);
                                return HTTPD_CGI_DONE;                  
                            }
                        }
                    }
                    //todo: stat(path+image,...) to test that it exists and is a file

                    // UPDATE disks[disk] entry
                    disks[disk] = image;
                    sendDisks(connData);
                    writeDiskmap();
                }
            }
            break;
        case HTTPD_METHOD_DELETE:
            ESP_LOGI(__func__, "DELETE /disks: %s", connData->getArgs==NULL?"":connData->getArgs);
            disk = getDiskID(connData);
            if(disk != -1) {
                //disk = ARG;
                if(disks[disk] == NULL) {
                    ESP_LOGW(__func__, "DELETE /disks ALREADY EMPTY");
                    httpdStartResponse(connData, 404);  //http error code 'Not Found'
                    httpdEndHeaders(connData);    
                } else {
                    // CLEAR disks[disk] entry
                    free(disks[disk]);
                    disks[disk] = NULL;
                    sendDisks(connData);
                    writeDiskmap();
                }
            }
            break;
        default:
            httpdStartResponse(connData, 405);  //http error code 'Method Not Allowed'
            httpdEndHeaders(connData);
            // return HTTPD_CGI_DONE;
	}

    return HTTPD_CGI_DONE;   
}

CgiStatus   cgiLibrary(HttpdConnData *connData) {
    struct dirent *pDirent;
    DIR *pDir;
    int len, i = 0;
    char output[512];	//Temporary buffer for HTML output
    
    if (connData->isConnectionClosed) {
		//Connection aborted. Clean up.
		return HTTPD_CGI_DONE;
	}

    switch(connData->requestType) {
        case HTTPD_METHOD_GET:
        /**
         * The good stuff happens here...
         */
        pDir = opendir (DISKSDIR);
        if (pDir == NULL) {
            httpdStartResponse(connData, 404);  //http error code 'Not Found'
            httpdEndHeaders(connData);
        } else {
            httpdStartResponse(connData, 200); 
            httpdHeader(connData, "Content-Type", "application/json");
            httpdEndHeaders(connData);
    
            httpdSend(connData, "{", -1);

            // todo: need to exclude disk images that are already mounted in disks[]
            while ((pDirent = readdir(pDir)) != NULL) {
                ESP_LOGI(pDirent->d_name, "GET /library: %d", pDirent->d_ino);
                if(pDirent->d_type==DT_REG) {
                    len=sprintf(output, "\"%d\": \"%s\",", i++, pDirent->d_name);
                    httpdSend(connData, output, len);
                 }
            }
            closedir (pDir);
            httpdSend(connData, "\"END\": \"END\"", -1);
            httpdSend(connData, "}", -1);
        }
            break;
        case HTTPD_METHOD_DELETE:
            // DELETE
            if(connData->post.len > 0) {
                ESP_LOGI(__func__, "POST(DELETE) length: %d, %s", connData->post.len, connData->post.buff);   
                strcpy(fn, connData->post.buff);
                /* check that file is not already in disks[] */
                for(i=0; i<4; i++) {
                    if(disks[i] != NULL) {
                        if(strcmp(fn, disks[i]) == 0) {
                            ESP_LOGW(__func__, "DELETE image: %s, currently inserted in disks", fn);   
                            httpdStartResponse(connData, 404);  //http error code 'Not Found'
                            httpdEndHeaders(connData);   
                            return HTTPD_CGI_DONE;                  
                        }
                    }
    }
                strcpy(fn, DISKSDIR);
                strcat(fn, "/");
                strcat(fn, connData->post.buff);

                if(unlink(fn) < 0) {
                    ESP_LOGE(__func__, "DELETE image: %s, unlink failed [%d]", fn, errno);   
                    httpdStartResponse(connData, 410);  //http error code 'Gone'
                    httpdEndHeaders(connData);   
                } else {
                    ESP_LOGI(__func__, "DELETE image: %s, deleted", fn);
                    httpdStartResponse(connData, 200); 
                    // httpdHeader(connData, "Content-Type", "application/json");
                    httpdEndHeaders(connData);
                    httpdSend(connData, "Deleted", -1);
                };
            }
            break;
        // case HTTPD_METHOD_UPDATE:
        //     // RENAME
        //     break;
        default:
            httpdStartResponse(connData, 405);  //http error code 'Method Not Allowed'
            httpdEndHeaders(connData);
            // return HTTPD_CGI_DONE;
    }

    return HTTPD_CGI_DONE;
}

#define DISKMAP "disk.map"

void readDiskmap(void) {
    FILE *stream;
    char *line = NULL;
    size_t len = 0;
    ssize_t nread;
    int i;

    strcpy(fn, DISKSDIR);
    strcat(fn, "/");
    strcat(fn, DISKMAP);
    stream = fopen(fn, "r");
    if (stream == NULL) {
        ESP_LOGW(__func__, "No disk map: %s", fn);
        return;
    }

    printf("DISK MAP: [%s]\n", fn);

    for(i=0;i<4;i++) {
        line = NULL;
        len = 0;
        nread = __getline(&line, &len, stream);

        if(nread != -1) {
            line[nread-1] = '\0';
            disks[i] = line[0]=='\0'?NULL:line;
            if(disks[i] != NULL) {
                printf("%c:DSK:='%s'\n", i+'A', disks[i]);
            }
        } else {
            free(line);
        }
    }
    fclose(stream);
}

void writeDiskmap(void) {
    FILE *stream;
    int i;

    strcpy(fn, DISKSDIR);
    strcat(fn, "/");
    strcat(fn, DISKMAP);
    stream = fopen(fn, "w");
    if (stream == NULL) {
        ESP_LOGI(__func__, "Can't create disk map: %s", fn);
        return;
    }

    for(i=0;i<4;i++) {
        fprintf(stream, "%s\n", disks[i]==NULL?"":disks[i]);
    }
    fclose(stream);  
}