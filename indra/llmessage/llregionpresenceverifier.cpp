/** 
 * @file llregionpresenceverifier.cpp
 * @brief 
 *
 * $LicenseInfo:firstyear=2008&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "llregionpresenceverifier.h"
#include "llhttpclientinterface.h"
#include <sstream>
#include "net.h"
#include "message.h"

//namespace boost
//{
	void intrusive_ptr_add_ref(LLRegionPresenceVerifier::Response* p)
	{
		nd::intrin::FAA( &p->mReferenceCount );
	}
	
	void intrusive_ptr_release(LLRegionPresenceVerifier::Response* p)
	{
		if(p && 0 == nd::intrin::FAD( &p->mReferenceCount ))
		{
			delete p;
		}
	}
//};

LLRegionPresenceVerifier::Response::~Response()
{
}

LLRegionPresenceVerifier::RegionResponder::RegionResponder(const std::string&
														   uri,
														   ResponsePtr data,
														   S32 retry_count) :
	mUri(uri),
	mSharedData(data),
	mRetryCount(retry_count)
{
}

//virtual
LLRegionPresenceVerifier::RegionResponder::~RegionResponder()
{
}

void LLRegionPresenceVerifier::RegionResponder::result(const LLSD& content)
{
	std::string host = content["private_host"].asString();
	U32 port = content["private_port"].asInteger();
	LLHost destination(host, port);
	LLUUID id = content["region_id"];

	LL_DEBUGS() << "Verifying " << destination.getString() << " is region " << id << LL_ENDL;

	std::stringstream uri;
	uri << "http://" << destination.getString() << "/state/basic/";
	mSharedData->getHttpClient().get(
		uri.str(),
		new VerifiedDestinationResponder(mUri, mSharedData, content, mRetryCount));
}

void LLRegionPresenceVerifier::RegionResponder::error(U32 status,
													 const std::string& reason)
{
	// TODO: babbage: distinguish between region presence service and
	// region verification errors?
	mSharedData->onRegionVerificationFailed();
}

LLRegionPresenceVerifier::VerifiedDestinationResponder::VerifiedDestinationResponder(const std::string& uri, ResponsePtr data, const LLSD& content,
	S32 retry_count):
	mUri(uri),
	mSharedData(data),
	mContent(content),
	mRetryCount(retry_count) 
{
}

//virtual
LLRegionPresenceVerifier::VerifiedDestinationResponder::~VerifiedDestinationResponder()
{
}

void LLRegionPresenceVerifier::VerifiedDestinationResponder::result(const LLSD& content)
{
	LLUUID actual_region_id = content["region_id"];
	LLUUID expected_region_id = mContent["region_id"];

	LL_DEBUGS() << "Actual region: " << content << LL_ENDL;
	LL_DEBUGS() << "Expected region: " << mContent << LL_ENDL;

	if (mSharedData->checkValidity(content) &&
		(actual_region_id == expected_region_id))
	{
		mSharedData->onRegionVerified(mContent);
	}
	else if (mRetryCount > 0)
	{
		retry();
	}
	else
	{
		LL_WARNS() << "Simulator verification failed. Region: " << mUri << LL_ENDL;
		mSharedData->onRegionVerificationFailed();
	}
}

void LLRegionPresenceVerifier::VerifiedDestinationResponder::retry()
{
	LLSD headers;
	headers["Cache-Control"] = "no-cache, max-age=0";
	LL_INFOS() << "Requesting region information, get uncached for region "
			<< mUri << LL_ENDL;
	--mRetryCount;
	mSharedData->getHttpClient().get(mUri, new RegionResponder(mUri, mSharedData, mRetryCount), headers);
}

void LLRegionPresenceVerifier::VerifiedDestinationResponder::error(U32 status, const std::string& reason)
{
	if(mRetryCount > 0)
	{
		retry();
	}
	else
	{
		LL_WARNS() << "Failed to contact simulator for verification. Region: " << mUri << LL_ENDL;
		mSharedData->onRegionVerificationFailed();
	}
}
