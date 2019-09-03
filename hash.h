#ifndef _HASH_H_
#define _HASH_H_

unsigned int ELFHash(char *data);
unsigned int RSHash(char *str);
unsigned int JSHash(char* str);
unsigned int PJWHash(char* str);
unsigned int BKDRHash(char* str);
unsigned int SDBMHash(char* str);
unsigned int DJBHash(char* str);
unsigned int APHash(char* str);

#endif

