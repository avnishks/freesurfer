#include <stdlib.h>
#include "string_fixed.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include "Broadcaster.h"
#include "Listener.h"
#include "Scuba-impl.h"


char* Progname = "test_Broadcaster";

using namespace std;

#define Assert(x,s)   \
  if(!(x)) { \
  stringstream ss; \
  ss << "Line " << __LINE__ << ": " << s; \
  cerr << ss.str().c_str() << endl; \
  throw runtime_error( ss.str() ); \
  }

class BroadcasterTester {
public:
  void Test();
};

class ListenerData {
public:
  int mData;
};

class TestListener : public Listener {
public:
  void Clear () { bGotMessage = false; bGotData = false; }
  virtual void DoListenToMessage ( string iMessage, void* iData );
  bool GotMessage () { return bGotMessage; }
  bool GotData () { return bGotData; }
protected:
  bool bGotMessage;
  bool bGotData;
};

void 
TestListener::DoListenToMessage ( string iMessage, void* iData ) {

  if( iMessage == "testMessage" ) {
    bGotMessage = true;
    if( NULL != iData ) {
      ListenerData* data = (ListenerData*)iData;
      if( data->mData == 5 ) {
	bGotData = true;
      }
    }
  }
}

void 
BroadcasterTester::Test () {

  stringstream ssError;

  try {

    Broadcaster broadcaster;
    TestListener listenerListening;
    TestListener listenerNotListening;

    broadcaster.AddListener( &listenerListening );

    ListenerData data;
    data.mData = 5;
    listenerListening.Clear();
    listenerNotListening.Clear();
    broadcaster.SendBroadcast( "testMessage", &data );

    Assert( (listenerListening.GotMessage()), "Listener didn't get message" );
    Assert( (listenerListening.GotData()), "Listener didn't get data" );
    Assert( (!listenerNotListening.GotMessage()), "NotListener got message" );
    
    listenerListening.Clear();
    listenerNotListening.Clear();
    broadcaster.SendBroadcast( "bogusMessage", NULL );

    Assert( (!listenerListening.GotMessage()), "Listener got bogus message" );
    Assert( (!listenerNotListening.GotMessage()), 
	    "NotListener got bogus message" );

    broadcaster.RemoveListener( &listenerListening );

    listenerListening.Clear();
    listenerNotListening.Clear();
    broadcaster.SendBroadcast( "testMessage", &data );

    Assert( (!listenerListening.GotMessage()), "Listener got message after being removed" );
    Assert( (!listenerNotListening.GotMessage()), "NotListener got message" );

  }
  catch( runtime_error& e ) {
    cerr << "failed with exception: " << e.what() << endl;
    exit( 1 );
  }
  catch(...) {
    cerr << "failed" << endl;
    exit( 1 );
  }
}


int main ( int argc, char** argv ) {

  cerr << "Beginning test" << endl;

  try {


    BroadcasterTester tester0;
    tester0.Test();

 
  }
  catch( runtime_error& e ) {
    cerr << "failed with exception: " << e.what() << endl;
    exit( 1 );
  }
  catch(...) {
    cerr << "failed" << endl;
    exit( 1 );
  }

  cerr << "Success" << endl;

  exit( 0 );
}

