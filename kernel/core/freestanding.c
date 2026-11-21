/* Minimal freestanding string helpers the compiler may emit calls to. */
typedef unsigned long uintptr_size_t_dummy;
void *memcpy(void *d,const void *s,unsigned long n){unsigned char *dd=d;const unsigned char *ss=s;while(n--)*dd++=*ss++;return d;}
void *memset(void *d,int c,unsigned long n){unsigned char *dd=d;while(n--)*dd++=(unsigned char)c;return d;}
int memcmp(const void *a,const void *b,unsigned long n){const unsigned char *x=a,*y=b;while(n--){if(*x!=*y)return *x-*y;x++;y++;}return 0;}
unsigned long strlen(const char *s){unsigned long n=0;while(s[n])n++;return n;}
int strcmp(const char *a,const char *b){while(*a&&*a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b;}
int strncmp(const char *a,const char *b,unsigned long n){while(n&&*a&&*a==*b){a++;b++;n--;}return n?(unsigned char)*a-(unsigned char)*b:0;}
char *strcpy(char *d,const char *s){char *r=d;while((*d++=*s++));return r;}
char *strncpy(char *d,const char *s,unsigned long n){char *r=d;while(n&&*s){*d++=*s++;n--;}while(n--)*d++=0;return r;}
