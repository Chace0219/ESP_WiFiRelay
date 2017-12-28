/* Created By jinzhouyun */

#ifndef MyFunc_h
#define MyFunc_h

struct tonblock
{
    unsigned IN: 1; // IN option
    unsigned PRE: 1; // IN option
    unsigned Q: 1; // Output
    unsigned long PT; // Set Timeout
    unsigned long ET; // Elapsed time
};
typedef struct tonblock TON;

struct tpblock
{
    unsigned IN: 1; // IN option
    unsigned PRE: 1; // PRE option
    unsigned Q: 1; // Output
    unsigned long PT; // Set Timeout
    unsigned long ET; // Elapsed time
};
typedef struct tpblock TP;

struct RisingTrg
{
    unsigned IN : 1;
    unsigned PRE : 1;
    unsigned Q : 1;
    unsigned : 5;
};
typedef struct RisingTrg Rtrg;

struct FallingTrg
{
    unsigned IN : 1;
    unsigned PRE : 1;
    unsigned Q : 1;
    unsigned : 5;
};
typedef struct FallingTrg Ftrg;



// When any condition is true, it returns true
void TONFunc(TON *pTP)
{
    if(pTP->IN != pTP->PRE)
    {
        pTP->PRE = pTP->IN;
        if(pTP->IN == 1)
            pTP->ET = millis();
    }
    
    if(pTP->IN)
    {
        if((pTP->ET + pTP->PT) <= millis())
            pTP->Q = 1;  
    }
    else
    {
        pTP->ET = millis();
        pTP->Q = 0;
    }
}

// When any condition is true, it returns true
void TPFunc(TP *pTP)
{
    if(pTP->IN != pTP->PRE)
    {
        pTP->PRE = pTP->IN;
        if(pTP->IN == 1)
            pTP->ET = pTP->PT + millis();
    }
    if(pTP->ET >= millis())
        pTP->Q = 1;
    else
    {
        pTP->Q = 0;
        pTP->ET = millis();  
    }
}

// It should be used with TONFunc together
void RTrgFunc(Rtrg *pTrg)
{
    pTrg->Q = 0;
    if(pTrg->IN != pTrg->PRE)
    {
        pTrg->PRE = pTrg->IN;
        if(pTrg->PRE == 1)
        {
            pTrg->Q = 1;
        }    
    }
}

// It should be used with TONFunc together
void FTrgFunc(Ftrg *pTrg)
{
    pTrg->Q = 0;
    if(pTrg->IN != pTrg->PRE)
    {
        pTrg->PRE = pTrg->IN;
        if(pTrg->IN == 0)
        {
            pTrg->Q = 1;
        }    
    }
}
#endif //MyFunc_h
