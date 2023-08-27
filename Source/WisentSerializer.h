#ifndef WISENTSERIALIZER_H
#define WISENTSERIALIZER_H

char* wisentLoad(char const* path, char const* sharedMemoryName, char const* csvPrefix);
void wisentUnload(char const* sharedMemoryName);
void wisentFree(char const* sharedMemoryName);

#endif /* WISENTSERIALIZER_H */
