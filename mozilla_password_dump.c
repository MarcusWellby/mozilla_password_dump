#ifdef UNICODE
#undef UNICODE
#endif

#ifndef _WIN32
#define MAX_PATH        300
#endif

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <stdlib.h>
#include <string.h>
#include <nss.h>
#include "sqlite3.h"
#include "pk11pub.h"
#include "nssb64.h"
#include "pk11sdr.h"
#include "iniparser.h"
#endif

#ifndef _WIN32
char *pathSeparator = "/";
#define TRUE PR_TRUE
#else
char *pathSeparator = "\\";
#endif

#ifdef _WIN32
typedef enum {
	siBuffer,
	siClearDataBuffer,
	siCipherDataBuffer,
	siDERCertBuffer,
	siEncodedCertBuffer,
	siDERNameBuffer,
	siEncodedNameBuffer,
	siAsciiNameString,
	siAsciiString,
	siDEROID
} SECItemType;

struct SECItemStr {
	SECItemType type;
	unsigned char *data;
	unsigned int len;
};

typedef enum {
	SECWouldBlock = -2,
	SECFailure = -1,
	SECSuccess = 0
} SECStatus;

typedef struct SECItemStr SECItem;


typedef SECStatus(*NSSInit) (char *);
typedef void *(*PK11GetInternalKeySlot) ();
typedef SECStatus(*PK11SDRDecrypt) (SECItem *, SECItem *, void *);
typedef SECStatus(*NSSBase64DecodeBuffer) (void *ptr, SECItem *, char *, unsigned int);
typedef SECStatus(*PK11Authenticate) (void *, int, void *);
typedef SECStatus(*PK11CheckUserPassword) (void *, char *);
typedef SECStatus(*NSSShutdown) ();
typedef void (*PK11FreeSlot) (void *);

NSSInit NSS_Init;
PK11GetInternalKeySlot PK11_GetInternalKeySlot;
PK11SDRDecrypt PK11SDR_Decrypt;
NSSBase64DecodeBuffer NSSBase64_DecodeBuffer;
PK11Authenticate PK11_Authenticate;
PK11CheckUserPassword PK11_CheckUserPassword;
NSSShutdown NSS_Shutdown;
PK11FreeSlot PK11_FreeSlot;
#endif

int loadFirefoxLibraries()
{
#ifdef _WIN32
	char pathFirefox[MAX_PATH];
	char pathDll[MAX_PATH];
	HMODULE moduleNSS;

	SHGetSpecialFolderPath(0, pathFirefox, CSIDL_PROGRAM_FILES, FALSE);
	strcat(pathFirefox, "\\Mozilla Firefox");

	sprintf(pathDll, "%s\\%s", pathFirefox, "mozcrt19.dll");
	if (!LoadLibrary(pathDll))
		return 1;

	sprintf(pathDll, "%s\\%s", pathFirefox, "sqlite3.dll");
	if (!LoadLibrary(pathDll))
		return 1;

	sprintf(pathDll, "%s\\%s", pathFirefox, "nspr4.dll");
	if (!LoadLibrary(pathDll))
		return 1;

	sprintf(pathDll, "%s\\%s", pathFirefox, "plc4.dll");
	if (!LoadLibrary(pathDll))
		return 1;

	sprintf(pathDll, "%s\\%s", pathFirefox, "plds4.dll");
	if (!LoadLibrary(pathDll))
		return 1;

	sprintf(pathDll, "%s\\%s", pathFirefox, "nssutil3.dll");
	if (!LoadLibrary(pathDll))
		return 1;

	sprintf(pathDll, "%s\\%s", pathFirefox, "softokn3.dll");
	if (!LoadLibrary(pathDll))
		return 1;

	sprintf(pathDll, "%s\\%s", pathFirefox, "nss3.dll");
	if (!(moduleNSS = LoadLibrary(pathDll)))
		return 1;

	NSS_Init = (NSSInit) GetProcAddress(moduleNSS, "NSS_Init");
	PK11_GetInternalKeySlot = (PK11GetInternalKeySlot) GetProcAddress(moduleNSS, "PK11_GetInternalKeySlot");
	PK11_Authenticate = (PK11Authenticate) GetProcAddress(moduleNSS, "PK11_Authenticate");
	PK11SDR_Decrypt = (PK11SDRDecrypt) GetProcAddress(moduleNSS, "PK11SDR_Decrypt");
	NSSBase64_DecodeBuffer = (NSSBase64DecodeBuffer) GetProcAddress(moduleNSS, "NSSBase64_DecodeBuffer");
	PK11_CheckUserPassword = (PK11CheckUserPassword) GetProcAddress(moduleNSS, "PK11_CheckUserPassword");
	NSS_Shutdown = (NSSShutdown) GetProcAddress(moduleNSS, "NSS_Shutdown");
	PK11_FreeSlot = (PK11FreeSlot) GetProcAddress(moduleNSS, "PK11_FreeSlot");
#endif
	return 0;
}


void PK11Decrypt(char *cipheredBuffer, char **plaintext)
{
	SECItem *request;
	SECItem *reply;
	unsigned int len = strlen(cipheredBuffer);

	reply = SECITEM_AllocItem(NULL, NULL, 0);

	request = NSSBase64_DecodeBuffer(NULL, NULL, cipheredBuffer, len);
	PK11SDR_Decrypt(request, reply, NULL);

	*plaintext = malloc(reply->len + 1);
	strncpy(*plaintext, (const char*)reply->data, reply->len);
	(*plaintext)[reply->len] = '\0';

	SECITEM_FreeItem(request, TRUE);
	SECITEM_FreeItem(reply, TRUE);
}

int get_profile(char *pathProfilesIni, char *profile)
{
#ifndef _WIN32
	dictionary *ini;
	ini = iniparser_load(pathProfilesIni);
	if (ini == NULL) {
		fprintf(stderr, "cannot parse file: %s\n", pathProfilesIni);
		return -1;
	}
	char *s = iniparser_getstring(ini, "Profile0:Path", NULL);
	int length = strlen(s);
	strncpy(profile, s, length + 1);

	iniparser_freedict(ini);
#else
	GetPrivateProfileString("Profile0", "Path", "", profile, MAX_PATH, pathProfilesIni);
#endif

	// printf("Found profile2 path: %s\n", profile);
	return 0;
}

static void usage(char **argv)
{
	printf(
			"Usage: %s [OPTIONS]\n\n"
			"Options:\n"
			"       -f  = Dump Firefox passwords\n"
			"       -t  = Dump Thunderbird passwords\n"
			"       -c <folder> = Dump passwords from profile present in given folder\n"
			"       -p <password> = Specify master password to use\n", argv[0]);
}

int main(int argc, char **argv)
{
	sqlite3 *db;
	char query[] = "SELECT * FROM moz_logins";
	sqlite3_stmt *stmt;
	char pathData[MAX_PATH];
	char pathProfilesIni[MAX_PATH];
	char profile[MAX_PATH];
	char pathProfile[MAX_PATH];
	char pathSignons[MAX_PATH];
	void *keySlot;
	int fflag = 0;
	int tflag = 0;
	int cflag = 0;
	int pflag = 0;
	char *folder = NULL;
	char *password = NULL;
	int c;
	if(argc < 2) {
		usage(argv);
		exit(-1);
	}
	opterr = 0;
	while((c = getopt(argc, argv, "ftc:p:")) != -1)
		switch (c)
		{
			case 'f':
				fflag = 1;
				break;
			case 't':
				tflag = 1;
				break;
			case 'c':
				cflag = 1;
				folder = optarg;
				break;
			case 'p':
				pflag = 1;
				password = optarg;
				break;
			case '?':
				if(optopt == 'c') {
					fprintf (stderr, "Option -%c requires an argument.\n\n", optopt);
					usage(argv);
				}
				else if(optopt == 'p') {
					fprintf (stderr, "Option -%c requires an argument.\n\n", optopt);
					usage(argv);
				}
				else if(optopt) {
					fprintf (stderr, "Unknown option requested\n\n");
					usage(argv);
				}
				return 1;
			default:
				abort();
		}

	if(!fflag && !tflag && !cflag) {
		fprintf (stderr, "Option -t or -f is required.\n\n");
		usage(argv);
		return 1;
	}

	if (loadFirefoxLibraries()) {
		fprintf(stderr, "loadFirefoxLibraries fails\r\n");
		fflush(stderr);
		return 1;
	}
#ifdef _WIN32
	if(fflag == 1) {
		SHGetSpecialFolderPath(0, pathData, CSIDL_APPDATA, FALSE);
		strcat(pathData, "\\Mozilla\\Firefox");
		sprintf(pathProfilesIni, "%s\\profiles.ini", pathData);
	}
	else if(tflag == 1) {
		SHGetSpecialFolderPath(0, pathData, CSIDL_APPDATA, FALSE);
		strcat(pathData, "\\Thunderbird");
		sprintf(pathProfilesIni, "%s\\profiles.ini", pathData);
	}
	else if(cflag == 1) {
		sprintf(pathProfilesIni, "%s\\profiles.ini", folder);
	}
#else
	char *home = getenv("HOME");
	if(fflag == 1) {
		sprintf(pathData, "%s/.mozilla/firefox", home);
		sprintf(pathProfilesIni, "%s/profiles.ini", pathData);
	}
	else if(tflag == 1) {
		sprintf(pathData, "%s/.thunderbird", home);
		sprintf(pathProfilesIni, "%s/profiles.ini", pathData);
	}
	else if(cflag == 1) {
		sprintf(pathData, "%s", folder);
		sprintf(pathProfilesIni, "%s/profiles.ini", folder);
	}
#endif
	get_profile(pathProfilesIni, profile);
	sprintf(pathProfile, "%s%s%s", pathData, pathSeparator, profile);
	sprintf(pathSignons, "%s%ssignons.sqlite", pathProfile, pathSeparator);

        /* printf("home\t%s\n", home);
        printf("pathProfile\t%s\n", pathProfile);
        printf("pathSignons\t%s\n", pathSignons);
        printf("pathProfilesIni\t%s\n", pathProfilesIni); */

	if (NSS_Init(pathProfile) != SECSuccess) {
		fprintf(stderr, "NSS_Init fails\r\n");
		fflush(stderr);
		return 1;
	}

	if ((keySlot = PK11_GetInternalKeySlot()) == NULL) {
		fprintf(stderr, "PK11_GetInternalKeySlot fails\r\n");
		fflush(stderr);
		return 1;
	}

	if(pflag) {
		if (PK11_CheckUserPassword(keySlot, password) != SECSuccess) {
			fprintf(stderr, "PK11_CheckUserPassword fails, re-check or remove supplied master password\r\n");
			fflush(stderr);
			return 1;
		}
	}
	else {
		if (PK11_CheckUserPassword(keySlot, "") != SECSuccess) {
			/* fprintf(stderr, "PK11_CheckUserPassword fails\r\n"); */
			fprintf(stderr, "Master password is set. Run with -p <password> option\r\n");
			fflush(stderr);
			return 1;
		}
	}

	if (PK11_Authenticate(keySlot, TRUE, NULL) != SECSuccess) {
		fprintf(stderr, "PK11_Authenticate fails\r\n");
		fflush(stderr);
		return 1;
	}

	if (sqlite3_open(pathSignons, &db) != SQLITE_OK) {
		fprintf(stderr, "sqlite3_open fails : %s\r\n", sqlite3_errmsg(db));
		fflush(stderr);
		return 1;
	}

	if (sqlite3_prepare_v2(db, query, sizeof(query), &stmt, NULL) != SQLITE_OK) {
		fprintf(stderr, "sqlite3_prepare fails : %s\r\n", sqlite3_errmsg(db));
		fflush(stderr);
		return 1;
	}

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		char *hostname = (char *) sqlite3_column_text(stmt, 1);
		char *httpRealm = (char *) sqlite3_column_text(stmt, 2);
		char *formSubmitURL = (char *) sqlite3_column_text(stmt, 3);
		char *usernameField = (char *) sqlite3_column_text(stmt, 4);
		char *passwordField = (char *) sqlite3_column_text(stmt, 5);
		char *cipheredLogin = (char *) sqlite3_column_text(stmt, 6);
		char *cipheredPassword = (char *) sqlite3_column_text(stmt, 7);
		char *plaintextLogin, *plaintextPassword;

		PK11Decrypt(cipheredLogin, &plaintextLogin);
		PK11Decrypt(cipheredPassword, &plaintextPassword);

		printf("%s,%s,%s,%s,%s,%s,%s\r\n", hostname, httpRealm,
		    formSubmitURL, usernameField, passwordField, plaintextLogin, plaintextPassword);

		free(plaintextLogin);
		free(plaintextPassword);
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db);

	PK11_FreeSlot(keySlot);

	NSS_Shutdown();

	return 0;
}
