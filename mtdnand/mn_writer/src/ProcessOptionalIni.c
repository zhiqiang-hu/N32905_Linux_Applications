#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "nand_sa_writer.h"


#define INI_BUF_SIZE    512
char iniBuf2[INI_BUF_SIZE];
int  buffer_current2 = 0, buffer_end2 = 0;    // position to buffer iniBuf2

IBR_BOOT_OPTIONAL_STRUCT_T  optional_ini_file;


UINT32 power(UINT32 x, UINT32 n)
{
    UINT32 i;
    UINT32 num = 1;

    for(i = 1; i <= n; i++)
        num *= x;
    return num;
}


UINT32 transfer_hex_string_to_int(char *str_name)
{
    char string[]="0123456789ABCDEF";
    UINT32 number[]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    UINT32 i, j;
    UINT32 str_number = 0;

    for(i = 0; i < strlen(str_name); i++)
    {
        for(j = 0; j < strlen(string); j++)
        {
            if(toupper(str_name[i]) == string[j])
            {
                str_number += power(16, (strlen(str_name)-1-i))* number[j];
                break;
            }
        }
    }
    return str_number;
}


/*-----------------------------------------------------------------------------
 * To parse one pair of setting and store to ptr_Ini_Config.
 * The format of a pair should be <hex address> = <hex value>
 *---------------------------------------------------------------------------*/
void parsePair(IBR_BOOT_OPTIONAL_STRUCT_T *ptr_Ini_Config, char *Cmd)
{
    char delim[] = " =\t";
    char *token;
    token = strtok(Cmd, delim);
    if (token != NULL)
        ptr_Ini_Config->Pairs[ptr_Ini_Config->Counter].address = transfer_hex_string_to_int(token);
    token = strtok(NULL, delim);
    if (token != NULL)
        ptr_Ini_Config->Pairs[ptr_Ini_Config->Counter].value = transfer_hex_string_to_int(token);
}


/*-----------------------------------------------------------------------------
 * To read one string line from file fd_ini and save it to Cmd.
 * Return:
 *      Successful  : OK
 *      Fail        : Error
 *---------------------------------------------------------------------------*/
static int readLine(FILE *fd_ini, char *Cmd)
{
    int nReadLen, i_cmd;

    i_cmd = 0;

    while (1)
    {
        //--- parse INI file in buffer iniBuf2[] that read in at previous fsReadFile().
        while(buffer_current2 < buffer_end2)
        {
            if (iniBuf2[buffer_current2] == 0x0D)
            {
                // DOS   use 0x0D 0x0A as end of line;
                // Linux use 0x0A as end of line;
                // To support both DOS and Linux, we treat 0x0A as real end of line and ignore 0x0D.
                buffer_current2++;
                continue;
            }
            else if (iniBuf2[buffer_current2] == 0x0A)   // Found end of line
            {
                Cmd[i_cmd] = 0;     // end of string
                buffer_current2++;
                return Successful;
            }
            else
            {
                Cmd[i_cmd] = iniBuf2[buffer_current2];
                buffer_current2++;
                i_cmd++;
            }
        }

        //--- buffer iniBuf2[] is empty here. Try to read more data from file to buffer iniBuf2[].

        // no more data to read since previous fsReadFile() cannot read buffer full
        if ((buffer_end2 < INI_BUF_SIZE) && (buffer_end2 > 0))
        {
            if (i_cmd > 0)
            {
                // return last line of INI file that without end of line
                Cmd[i_cmd] = 0;     // end of string
                return Successful;
            }
            else
            {
                Cmd[i_cmd] = 0;     // end of string to clear Cmd
                return Fail;
            }
        }
        else
        {
            nReadLen = fread(iniBuf2, 1, INI_BUF_SIZE, fd_ini);
            if (nReadLen == 0)
            {
                Cmd[i_cmd] = 0;     // end of string to clear Cmd
                return Fail;      // error or end of file
            }
            buffer_current2 = 0;
            buffer_end2 = nReadLen;
        }
    }   // end of while (1)
}


/*-----------------------------------------------------------------------------
 * To parse INI file and store configuration to global variable optional_ini_file.
 * Return:
 *      Successful  : OK
 *      Fail        : Error
 *---------------------------------------------------------------------------*/
int ProcessOptionalINI(char *fileName)
{
    char Cmd[256];
    int  status;
    FILE *fd_ini;
    int i;
    IBR_BOOT_OPTIONAL_STRUCT_T *ptr_Ini_Config;

    ptr_Ini_Config = &optional_ini_file;

    //--- initial default value
    memset(ptr_Ini_Config->Pairs, 0xFF, sizeof(ptr_Ini_Config->Pairs));
    ptr_Ini_Config->OptionalMarker  = IBR_BOOT_CODE_OPTIONAL_MARKER;
    ptr_Ini_Config->Counter         = 0;

    //--- open INI file
    fd_ini = fopen(fileName, "r");
    if (fd_ini == NULL)
    {
        printf("ERROR: fail to open Optional INI file %s !\n", fileName);
        return Fail;
    }

    //--- parse INI file
    buffer_current2 = 0;
    buffer_end2 = 0;    // reset position to buffer iniBuf2 in readLine()
    do {
        status = readLine(fd_ini, Cmd);
        if (status < 0)     // read file error. Coulde be end of file.
            break;
NextMark2:
        if ((strcmp(Cmd, "[USER_DEFINE]") == 0) || (strcmp(Cmd, "[N3290 USER_DEFINE]") == 0))
        {
            do {
                status = readLine(fd_ini, Cmd);
                if (status < 0)
                    break;          // use default value since error code from NVTFAT. Coulde be end of file.
                else if (Cmd[0] == 0)
                    continue;       // skip empty line
                else if ((Cmd[0] == '/') && (Cmd[1] == '/'))
                    continue;       // skip comment line
                else if (Cmd[0] == '[')
                    goto NextMark2; // use default value since no assign value before next keyword
                else
                {
                    // printf("got pair #%d [%s]\n", ptr_Ini_Config->Counter, Cmd);
                    if (ptr_Ini_Config->Counter >= IBR_BOOT_CODE_OPTIONAL_MAX_NUMBER)
                    {
                        printf("ERROR: The number of Boot Code Optional Setting pairs cannot > %d !\n", IBR_BOOT_CODE_OPTIONAL_MAX_NUMBER);
                        printf("       Some pairs be ignored !\n");
                        break;
                    }
                    parsePair(ptr_Ini_Config, Cmd);
                    ptr_Ini_Config->Counter++;
                }
            } while (1);
        }
    } while (status >= 0);  // keep parsing INI file

    fclose(fd_ini);

    //--- show final configuration
    printf("Process %s file ...\n", fileName);
    printf("    OptionalMarker = 0x%08X\n", ptr_Ini_Config->OptionalMarker);
    printf("    Counter        = %d\n",     ptr_Ini_Config->Counter);
    for (i = 0; i < ptr_Ini_Config->Counter; i++)
        printf("    Pair %d: Address = 0x%08X, Value = 0x%08X\n", i, ptr_Ini_Config->Pairs[i].address, ptr_Ini_Config->Pairs[i].value);

    return Successful;
}
