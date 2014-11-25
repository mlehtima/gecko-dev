/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EmbedLog.h"
#include "mozilla/ipc/IOThreadChild.h"

#include "EmbedLiteContentProcess.h"

using mozilla::ipc::IOThreadChild;

namespace mozilla {
namespace embedlite {

void
EmbedLiteContentProcess::SetAppDir(const nsACString& aPath)
{
  mXREEmbed.SetAppDir(aPath);
}

bool
EmbedLiteContentProcess::Init()
{
  LOGT();
  mContent.Init(IOThreadChild::message_loop(),
                ParentHandle(),
                IOThreadChild::channel());

  mXREEmbed.Start();
  mContent.InitXPCOM();

  return true;
}

void
EmbedLiteContentProcess::CleanUp()
{
  LOGT();
  mXREEmbed.Stop();
}

} // namespace embedlite
} // namespace mozilla
