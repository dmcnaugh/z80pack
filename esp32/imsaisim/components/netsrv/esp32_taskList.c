#include <unistd.h>
#include <string.h>
#include "esp_log.h"
#include "esp_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "libesphttpd/esp.h"
#include "libesphttpd/httpd.h"

/*
 * Macros used by vListTask to indicate which state a task is in.
 */
#define tskBLOCKED_CHAR		( 'B' )
#define tskREADY_CHAR		( 'R' )
#define tskDELETED_CHAR		( 'D' )
#define tskSUSPENDED_CHAR	( 'S' )

#define HTTPD_PRINTF(args...)   sprintf(pcWriteBuffer, args); httpdSend(connData, pcWriteBuffer, -1);

void taskGetRunTimeStats( HttpdConnData *connData, char *pcWriteBuffer )
{
TaskStatus_t *pxTaskStatusArray;
volatile UBaseType_t uxArraySize, x;
uint32_t ulTotalTime, ulStatsAsPercentage;
char cStatus;

    /* Make sure the write buffer does not contain a string. */
    *pcWriteBuffer = 0x00;

    /* Take a snapshot of the number of tasks in case it changes while this
    function is executing. */
    uxArraySize = uxTaskGetNumberOfTasks();

    /* Allocate an array index for each task.  NOTE!  If
    configSUPPORT_DYNAMIC_ALLOCATION is set to 0 then pvPortMalloc() will
    equate to NULL. */
    pxTaskStatusArray = pvPortMalloc( uxArraySize * sizeof( TaskStatus_t ) );

    if( pxTaskStatusArray != NULL )
    {
        /* Generate the (binary) data. */
        uxArraySize = uxTaskGetSystemState( pxTaskStatusArray, uxArraySize, &ulTotalTime );

        /* For percentage calculations. */
        ulTotalTime /= 100UL;

        /* Avoid divide by zero errors. */
        if( ulTotalTime > 0 )
        {
            /* Create a human readable table from the binary data. */
            for( x = 0; x < uxArraySize; x++ )
            {
                switch( pxTaskStatusArray[ x ].eCurrentState )
				{
					case eReady:		cStatus = tskREADY_CHAR;
										break;

					case eBlocked:		cStatus = tskBLOCKED_CHAR;
										break;

					case eSuspended:	cStatus = tskSUSPENDED_CHAR;
										break;

					case eDeleted:		cStatus = tskDELETED_CHAR;
										break;

					default:			/* Should not get here, but it is included
										to prevent static checking errors. */
										cStatus = 0x00;
										break;
				}

				/* Write the task name to the string, padding with spaces so it
				can be printed in tabular form more easily. */
				// pcWriteBuffer = prvWriteNameToBuffer( pcWriteBuffer, pxTaskStatusArray[ x ].pcTaskName );
 				// sprintf( pcWriteBuffer, "%16s", 
 				if(x != 0) HTTPD_PRINTF(",");
                 
                HTTPD_PRINTF("{ \"name\": \"%s\",", 
                    pxTaskStatusArray[ x ].pcTaskName );
				// pcWriteBuffer += strlen( pcWriteBuffer );

				/* Write the rest of the string. */
				// sprintf( pcWriteBuffer, " %c %2u %6u %2u %3hd", 
				HTTPD_PRINTF("\"state\": \"%c\", \"pri\": %u, \"stack\": %u,  \"num\": %u, \"core\": %hd, ", 
                    cStatus, 
                    ( unsigned int ) pxTaskStatusArray[ x ].uxCurrentPriority, 
                    ( unsigned int ) pxTaskStatusArray[ x ].usStackHighWaterMark, 
                    ( unsigned int ) pxTaskStatusArray[ x ].xTaskNumber, 
                    ( int ) pxTaskStatusArray[ x ].xCoreID 
                );
				// pcWriteBuffer += strlen( pcWriteBuffer );

                /* What percentage of the total run time has the task used?
                This will always be rounded down to the nearest integer.
                ulTotalRunTimeDiv100 has already been divided by 100. */
                /* Also need to consider total run time of all */
                // ulStatsAsPercentage = (pxTaskStatusArray[ x ].ulRunTimeCounter/portNUM_PROCESSORS)/ ulTotalTime;
                ulStatsAsPercentage = (pxTaskStatusArray[ x ].ulRunTimeCounter)/ ulTotalTime;

                if( ulStatsAsPercentage > 0UL )
                {
                      /* sizeof( int ) == sizeof( long ) so a smaller
                        printf() library can be used. */
                        // sprintf( pcWriteBuffer, "\t%lu\t\t%lu%%\r\n", pxTaskStatusArray[ x ].ulRunTimeCounter, ulStatsAsPercentage );
                        // sprintf( pcWriteBuffer, " %12u %2u%%\r\n", 
                        HTTPD_PRINTF("\"count\": %u, \"pct\": %u }", 
                            ( unsigned int ) pxTaskStatusArray[ x ].ulRunTimeCounter, ( unsigned int ) ulStatsAsPercentage );
                }
                else
                {
                    /* If the percentage is zero here then the task has
                    consumed less than 1% of the total run time. */
                        /* sizeof( int ) == sizeof( long ) so a smaller
                        printf() library can be used. */
                        // sprintf( pcWriteBuffer, "\t%lu\t\t<1%%\r\n", pxTaskStatusArray[ x ].ulRunTimeCounter );
                        // sprintf( pcWriteBuffer, " %12u <1%%\r\n", 
                        HTTPD_PRINTF("\"count\": %u, \"pct\": %u }",
                            ( unsigned int ) pxTaskStatusArray[ x ].ulRunTimeCounter, ( unsigned int ) 0);
                }

                // pcWriteBuffer += strlen( pcWriteBuffer );
            }
            // pcWriteBuffer -= 1;
            // *pcWriteBuffer = 0x00;

        }

        /* Free the array again.  NOTE!  If configSUPPORT_DYNAMIC_ALLOCATION
        is 0 then vPortFree() will be #defined to nothing. */
        vPortFree( pxTaskStatusArray );
    }
}
