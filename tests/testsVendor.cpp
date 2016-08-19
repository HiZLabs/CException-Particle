#include "application.h"
#include "unit-test/unit-test.h"
#include "CException/CException.h"

//map Unity tests to Particle tests
#define TEST_FAIL_MESSAGE(m) { LOG(ERROR, m); fail(); }
#define TEST_ASSERT_EQUAL(x, y) assertEqual(x, y)
#define TEST_ASSERT_EQUAL_VOL_UINT(x, y) TEST_ASSERT_EQUAL(((unsigned int)x),((unsigned int)y))
#define TEST_ASSERT_FALSE(x) assertFalse(x)

volatile int TestingTheFallback;
volatile int TestingTheFallbackId;

volatile bool __cexception_hangOnUnHandledGlobalException = true;

bool __cexception_internal_global_handler(CEXCEPTION_T e) {
	TestingTheFallbackId = e;
	TestingTheFallback--;
	return __cexception_hangOnUnHandledGlobalException;
}

static void setUp(void)
{
    CExceptionFrames[0].pFrame = NULL;
    TestingTheFallback = 0;
    __cexception_hangOnUnHandledGlobalException = false;
}

static void tearDown(void)
{
	__cexception_hangOnUnHandledGlobalException = true;
}

test(CException_Group0_BasicTryDoesNothingIfNoThrow)
{
  setUp();
  int i = 0;
  CEXCEPTION_T e = 0x5a;

  Try
  {
    i += 1;
  }
  Catch(e)
  {
    TEST_FAIL_MESSAGE("Should Not Enter Catch If Not Thrown");
  }

  //verify that e was untouched
  TEST_ASSERT_EQUAL(0x5a, e);

  // verify that i was incremented once
  TEST_ASSERT_EQUAL(1, i);
  tearDown();
}

test(CException_Group0_BasicThrowAndCatch)
{
  setUp();
  CEXCEPTION_T e;

  Try
  {
    Throw(0xBE);
    TEST_FAIL_MESSAGE("Should Have Thrown An Error");
  }
  Catch(e)
  {
    //verify that e has the right data
    TEST_ASSERT_EQUAL(0xBE, e);
  }

  //verify that e STILL has the right data
  TEST_ASSERT_EQUAL(0xBE, e);
  tearDown();
}

test(CException_Group0_BasicThrowAndCatch_WithMiniSyntax)
{

  setUp();
  CEXCEPTION_T e;

  //Mini Throw and Catch
  Try
    Throw(0xEF);
  Catch(e)
    TEST_ASSERT_EQUAL(0xEF, e);
  TEST_ASSERT_EQUAL(0xEF, e);

  //Mini Passthrough
  Try
    e = 0;
  Catch(e)
    TEST_FAIL_MESSAGE("I shouldn't be caught because there was no throw");

  TEST_ASSERT_EQUAL(0, e);
  tearDown();
}

test(CException_Group0_VerifyVolatilesSurviveThrowAndCatch)
{
  setUp();
  volatile unsigned int VolVal = 0;
  CEXCEPTION_T e;

  Try
  {
    VolVal = 2;
    Throw(0xBF);
    TEST_FAIL_MESSAGE("Should Have Thrown An Error");
  }
  Catch(e)
  {
    VolVal += 2;
    TEST_ASSERT_EQUAL(0xBF, e);
  }

  TEST_ASSERT_EQUAL_VOL_UINT(4, VolVal);
  TEST_ASSERT_EQUAL(0xBF, e);
  tearDown();
}

void HappyExceptionThrower(unsigned int ID)
{
  if (ID != 0)
  {
    Throw(ID);
  }
}

test(CException_Group0_ThrowFromASubFunctionAndCatchInRootFunc)
{
  setUp();
  volatile  unsigned int ID = 0;
  CEXCEPTION_T e;

  Try
  {

    HappyExceptionThrower(0xBA);
    TEST_FAIL_MESSAGE("Should Have Thrown An Exception");
  }
  Catch(e)
  {
    ID = e;
  }

  //verify that I can pass that value to something else
  TEST_ASSERT_EQUAL(0xBA, e);
  //verify that ID and e have the same value
  TEST_ASSERT_EQUAL_VOL_UINT(ID, e);
  tearDown();
}

void HappyExceptionRethrower(unsigned int ID)
{
  CEXCEPTION_T e;

  Try
  {
    Throw(ID);
  }
  Catch(e)
  {
    switch (e)
    {
    case 0xBD:
      Throw(0xBF);
      break;
    default:
      break;
    }
  }
}

test(CException_Group0_ThrowAndCatchFromASubFunctionAndRethrowToCatchInRootFunc)
{
  setUp();
  volatile  unsigned int ID = 0;
  CEXCEPTION_T e;

  Try
  {
    HappyExceptionRethrower(0xBD);
    TEST_FAIL_MESSAGE("Should Have Rethrown Exception");
  }
  Catch(e)
  {
    ID = 1;
  }

  TEST_ASSERT_EQUAL(0xBF, e);
  TEST_ASSERT_EQUAL_VOL_UINT(1, ID);
  tearDown();
}

test(CException_Group0_ThrowAndCatchFromASubFunctionAndNoRethrowToCatchInRootFunc)
{
  setUp();
  CEXCEPTION_T e = 3;

  Try
  {
    HappyExceptionRethrower(0xBF);
  }
  Catch(e)
  {
    TEST_FAIL_MESSAGE("Should Not Have Re-thrown Error (it should have already been caught)");
  }

  //verify that THIS e is still untouched, even though subfunction was touched
  TEST_ASSERT_EQUAL(3, e);
  tearDown();
}

test(CException_Group0_ThrowAnErrorThenEnterATryBlockFromWithinCatch_VerifyThisDoesntCorruptExceptionId)
{
  setUp();
  CEXCEPTION_T e;

  Try
  {
    HappyExceptionThrower(0xBF);
    TEST_FAIL_MESSAGE("Should Have Thrown Exception");
  }
  Catch(e)
  {
    TEST_ASSERT_EQUAL(0xBF, e);
    HappyExceptionRethrower(0x12);
    TEST_ASSERT_EQUAL(0xBF, e);
  }
  TEST_ASSERT_EQUAL(0xBF, e);
  tearDown();
}

test(CException_Group0_ThrowAnErrorThenEnterATryBlockFromWithinCatch_VerifyThatEachExceptionIdIndependent)
{
  setUp();
  CEXCEPTION_T e1, e2;

  Try
  {
    HappyExceptionThrower(0xBF);
    TEST_FAIL_MESSAGE("Should Have Thrown Exception");
  }
  Catch(e1)
  {
    TEST_ASSERT_EQUAL(0xBF, e1);
    Try
    {
      HappyExceptionThrower(0x12);
    }
    Catch(e2)
    {
      TEST_ASSERT_EQUAL(0x12, e2);
    }
    TEST_ASSERT_EQUAL(0x12, e2);
    TEST_ASSERT_EQUAL(0xBF, e1);
  }
  TEST_ASSERT_EQUAL(0x12, e2);
  TEST_ASSERT_EQUAL(0xBF, e1);
  tearDown();
}

test(CException_Group0_CanHaveMultipleTryBlocksInASingleFunction)
{
  setUp();
  CEXCEPTION_T e;

  Try
  {
    HappyExceptionThrower(0x01);
    TEST_FAIL_MESSAGE("Should Have Thrown Exception");
  }
  Catch(e)
  {
    TEST_ASSERT_EQUAL(0x01, e);
  }

  Try
  {
    HappyExceptionThrower(0xF0);
    TEST_FAIL_MESSAGE("Should Have Thrown Exception");
  }
  Catch(e)
  {
    TEST_ASSERT_EQUAL(0xF0, e);
  }
  tearDown();
}

test(CException_Group0_CanHaveNestedTryBlocksInASingleFunction_ThrowInside)
{
  setUp();
  int i = 0;
  CEXCEPTION_T e;

  Try
  {
    Try
    {
      HappyExceptionThrower(0x01);
      i = 1;
      TEST_FAIL_MESSAGE("Should Have Rethrown Exception");
    }
    Catch(e)
    {
      TEST_ASSERT_EQUAL(0x01, e);
    }
  }
  Catch(e)
  {
    TEST_FAIL_MESSAGE("Should Have Been Caught By Inside Catch");
  }

  // verify that i is still zero
  TEST_ASSERT_EQUAL(0, i);
  tearDown();
}

test(CException_Group0_CanHaveNestedTryBlocksInASingleFunction_ThrowOutside)
{
  setUp();
  int i = 0;
  CEXCEPTION_T e;

  Try
  {
    Try
    {
      i = 2;
    }
    Catch(e)
    {
      TEST_FAIL_MESSAGE("Should Not Be Caught Here");
    }
    HappyExceptionThrower(0x01);
    TEST_FAIL_MESSAGE("Should Have Rethrown Exception");
  }
  Catch(e)
  {
    TEST_ASSERT_EQUAL(0x01, e);
  }

  // verify that i is 2
  TEST_ASSERT_EQUAL(2, i);
  tearDown();
}

test(CException_Group0_AThrowWithoutATryCatchWillUseDefaultHandlerIfSpecified)
{
  setUp();
    //Let the fallback handler know we're expecting it to get called this time, so don't fail
    TestingTheFallback = 1;

    Throw(0xBE);

    //We know the fallback was run because it decrements the counter above
    TEST_ASSERT_FALSE(TestingTheFallback);
    TEST_ASSERT_EQUAL_VOL_UINT(0xBE, TestingTheFallbackId);
  tearDown();
}

test(CException_Group0_AThrowWithoutOutsideATryCatchWillUseDefaultHandlerEvenAfterTryCatch)
{
  setUp();
    CEXCEPTION_T e;

    Try
    {
        //It's not really important that we do anything here.
    }
    Catch(e)
    {
        //The entire purpose here is just to make sure things get set back to using the default handler when done
    }

    //Let the fallback handler know we're expecting it to get called this time, so don't fail
    TestingTheFallback = 1;

    Throw(0xBE);

    //We know the fallback was run because it decrements the counter above
    TEST_ASSERT_FALSE(TestingTheFallback);
    TEST_ASSERT_EQUAL_VOL_UINT(0xBE, TestingTheFallbackId);
  tearDown();
}

test(CException_Group0_AbilityToExitTryWithoutThrowingAnError)
{
  setUp();
    int i=0;
    CEXCEPTION_T e;

    Try
    {
        ExitTry();
        i = 1;
        TEST_FAIL_MESSAGE("Should Have Exited Try Before This");
    }
    Catch(e)
    {
        i = 2;
        TEST_FAIL_MESSAGE("Should Not Have Been Caught");
    }

    // verify that i is still zero
    TEST_ASSERT_EQUAL(0, i);
  tearDown();
}

test(CException_Group0_AbilityToExitTryWillOnlyExitOneLevel)
{
    setUp();
    int i=0;
    CEXCEPTION_T e;
    CEXCEPTION_T e2;

    Try
    {
        Try
        {
            ExitTry();
            TEST_FAIL_MESSAGE("Should Have Exited Try Before This");
        }
        Catch(e)
        {
            TEST_FAIL_MESSAGE("Should Not Have Been Caught By Inside");
        }
        i = 1;
    }
    Catch(e2)
    {
        TEST_FAIL_MESSAGE("Should Not Have Been Caught By Outside");
    }

    // verify that we picked up and ran after first Try
    TEST_ASSERT_EQUAL(1, i);
  tearDown();
}


