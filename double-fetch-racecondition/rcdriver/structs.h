#pragma once

typedef struct
{
	int* UserAddress;
}Ustruct;

typedef struct
{
	int field1;
	Ustruct* ustruct;
	int field3;
	int field4;
}UserStruct,*PUserStruct;
