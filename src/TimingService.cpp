#include "TimingService.h"
#include "Config.h"
#include "Application.h"

#include <iostream>
#include <ctime>
#include <boost/bind.hpp>

using namespace std;
using namespace boost;

/*
Only needs to start once we determine the network time
*/

TimingService::TimingService() : mLedgerTimer(NULL), mPropTimer(NULL), mValidTimer(NULL)
{
}

void TimingService::start(boost::asio::io_service& ioService)
{
	// TODO: calculate the amount of seconds left in the current ledger
	mLedgerTimer=new asio::deadline_timer(ioService, posix_time::seconds(theConfig.LEDGER_SECONDS)),
	mLedgerTimer->async_wait(boost::bind(&TimingService::handleLedger, this));

	mPropTimer=new asio::deadline_timer(ioService, posix_time::seconds(theConfig.LEDGER_PROPOSAL_DELAY_SECONDS));
	
}

void TimingService::handleLedger()
{
	cout << "publish ledger" << endl;
#if 0
	theApp->getLedgerMaster().startFinalization();
	mLedgerTimer->expires_at(mLedgerTimer->expires_at() + boost::posix_time::seconds(theConfig.LEDGER_SECONDS));
	mLedgerTimer->async_wait(boost::bind(&TimingService::handleLedger, this));

	mPropTimer->expires_at(mLedgerTimer->expires_at() + boost::posix_time::seconds(theConfig.LEDGER_PROPOSAL_DELAY_SECONDS));
	mPropTimer->async_wait(boost::bind(&TimingService::handleProp, this));
#endif
}

void TimingService::handleProp()
{
//	theApp->getLedgerMaster().sendProposal();
}

void TimingService::handleValid()
{
//	theApp->getLedgerMaster().endFinalization();
}

int TimingService::getCurrentLedgerIndex()
{
	return( (time(NULL)-theConfig.NETWORK_START_TIME)/theConfig.LEDGER_SECONDS );
}
