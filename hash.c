#include "hash.h"

unsigned int ELFHash(char *str)
{
	unsigned int hash = 0;
	unsigned int x = 0;

	while( *str )
	{
		hash = (hash<<4) + *str;
		if( (x=hash & 0xf0000000) != 0 )
		{
			hash ^= (x>>24);
			hash &= ~x;
		}
		str++;
	}

	return (hash & 0x7fffffff);
}

unsigned int RSHash(char* str) 
{ 
	unsigned int b = 378551; 
	unsigned int a = 63689; 
	unsigned int hash = 0;   
	while(*str) 
	{ 
		hash=hash*a+(*str++); 
		a*=b; 
	}

	return hash; 
}

unsigned int JSHash(char* str)
{ 
	unsigned int hash=1315423911;    
	while(*str) 
	{ 
		hash^=((hash<<5)+(*str++)+(hash>>2)); 
	}

	return hash; 
} 

unsigned int PJWHash(char* str)
{ 
	unsigned int BitsInUnignedInt=(unsigned int)(sizeof(unsigned int)*8); 
	unsigned int ThreeQuarters=(unsigned int)((BitsInUnignedInt*3)/4); 
	unsigned int OneEighth=(unsigned int)(BitsInUnignedInt/8); 
	unsigned int HighBits=(unsigned int)(0xFFFFFFFF)<<(BitsInUnignedInt-OneEighth); 
	unsigned int hash=0; 
	unsigned int test=0;

	while(*str) 
	{ 
		hash=(hash<<OneEighth)+(*str++); 
		if((test=hash&HighBits)!=0) 
		{ 
			hash=((hash^(test>>ThreeQuarters))&(~HighBits)); 
		} 
	}      

	return hash; 
} 

unsigned int BKDRHash(char* str)
{ 
	unsigned int seed=131313; // 31 131 1313 13131 131313 ..  
	unsigned int hash=0;     
	while(*str) 
	{ 
		hash=hash*seed+(*str++); 
	}

	return hash; 
} 
 
unsigned int SDBMHash(char* str)
{ 
	unsigned int hash=0;

	while(*str) 
	{ 
		hash=(*str++)+(hash<<6)+(hash<<16)-hash; 
	}

	return hash; 
}

unsigned int DJBHash(char* str)
{ 
	unsigned int hash = 5381;

	while(*str) 
	{ 
		hash+=(hash<<5)+(*str++); 
	}

	return hash; 
}

unsigned int APHash(char* str)
{ 
	unsigned int hash = 0; 
	int i = 0;

	for( ; *str; i++) 
	{ 
		if((i&1)==0) 
		{ 
			hash^=((hash<<7)^(*str++)^(hash>>3)); 
		} 
		else  
		{ 
			hash^=(~((hash<<11)^(*str++)^(hash>>5))); 
		} 
	}

	return hash;
}


