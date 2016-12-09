/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EmbedContentController.h"
#include "EmbedLog.h"
#include "EmbedLiteView.h"
#include "mozilla/unused.h"
#include "EmbedLiteViewBaseParent.h"
#include "mozilla/layers/CompositorParent.h"
#include "mozilla/layers/APZCTreeManager.h"
#include "EmbedLiteCompositorParent.h"

using namespace mozilla::embedlite;
using namespace mozilla::gfx;
using namespace mozilla::layers;

class FakeListener : public EmbedLiteViewListener {};

EmbedContentController::EmbedContentController(EmbedLiteViewBaseParent* aRenderFrame, MessageLoop* aUILoop)
  : mUILoop(aUILoop)
  , mRenderFrame(aRenderFrame)
{
}

void EmbedContentController::SetManagerByRootLayerTreeId(uint64_t aRootLayerTreeId)
{
  mAPZC = CompositorParent::GetAPZCTreeManager(aRootLayerTreeId);
  LOGT("APZCTreeManager: %p\n", mAPZC.get());
}

void EmbedContentController::RequestContentRepaint(const FrameMetrics& aFrameMetrics)
{
  // We always need to post requests into the "UI thread" otherwise the
  // requests may get processed out of order.
  LOGT();
  mUILoop->PostTask(
    FROM_HERE,
              NewRunnableMethod(this, &EmbedContentController::DoRequestContentRepaint, aFrameMetrics));
}

void EmbedContentController::RequestFlingSnap(const FrameMetrics::ViewID &aScrollId, const mozilla::CSSPoint &aDestination)
{
  LOGT();
}

void EmbedContentController::HandleDoubleTap(const CSSPoint& aPoint,
                                             Modifiers aModifiers,
                                             const ScrollableLayerGuid& aGuid)
{
  if (MessageLoop::current() != mUILoop) {
    // We have to send this message from the "UI thread" (main
    // thread).
    mUILoop->PostTask(
      FROM_HERE,
      NewRunnableMethod(this, &EmbedContentController::HandleDoubleTap, aPoint, aModifiers, aGuid));
    return;
  }
  if (mRenderFrame && !GetListener()->HandleDoubleTap(nsIntPoint(aPoint.x, aPoint.y))) {
    Unused << mRenderFrame->SendHandleDoubleTap(aPoint, aModifiers, aGuid);
  }
}

void EmbedContentController::HandleSingleTap(const CSSPoint& aPoint,
                                             Modifiers aModifiers,
                                             const ScrollableLayerGuid& aGuid)
{
  if (MessageLoop::current() != mUILoop) {
    // We have to send this message from the "UI thread" (main
    // thread).
    mUILoop->PostTask(
      FROM_HERE,
      NewRunnableMethod(this, &EmbedContentController::HandleSingleTap, aPoint, aModifiers, aGuid));
    return;
  }
  if (mRenderFrame && !GetListener()->HandleSingleTap(nsIntPoint(aPoint.x, aPoint.y))) {
    Unused << mRenderFrame->SendHandleSingleTap(aPoint, aModifiers, aGuid);
  }
}

void EmbedContentController::HandleLongTap(const CSSPoint& aPoint,
                                           Modifiers aModifiers,
                                           const ScrollableLayerGuid& aGuid,
                                           uint64_t aInputBlockId)
{
  if (MessageLoop::current() != mUILoop) {
    // We have to send this message from the "UI thread" (main
    // thread).
    mUILoop->PostTask(
      FROM_HERE,
      NewRunnableMethod(this, &EmbedContentController::HandleLongTap, aPoint, aModifiers, aGuid, aInputBlockId));
    return;
  }
  if (mRenderFrame && !GetListener()->HandleLongTap(nsIntPoint(aPoint.x, aPoint.y))) {
    Unused << mRenderFrame->SendHandleLongTap(aPoint, aGuid, aInputBlockId);
  }
}

/**
 * Requests sending a mozbrowserasyncscroll domevent to embedder.
 * |aContentRect| is in CSS pixels, relative to the current cssPage.
 * |aScrollableSize| is the current content width/height in CSS pixels.
 */
void EmbedContentController::SendAsyncScrollDOMEvent(bool aIsRoot,
                                                     const CSSRect& aContentRect,
                                                     const CSSSize& aScrollableSize)
{
  if (MessageLoop::current() != mUILoop) {
    // We have to send this message from the "UI thread" (main
    // thread).
    mUILoop->PostTask(
      FROM_HERE,
      NewRunnableMethod(this, &EmbedContentController::SendAsyncScrollDOMEvent,
                        aIsRoot, aContentRect, aScrollableSize));
    return;
  }
  LOGNI("contentR[%g,%g,%g,%g], scrSize[%g,%g]",
    aContentRect.x, aContentRect.y, aContentRect.width, aContentRect.height,
    aScrollableSize.width, aScrollableSize.height);
  gfxRect rect(aContentRect.x, aContentRect.y, aContentRect.width, aContentRect.height);
  gfxSize size(aScrollableSize.width, aScrollableSize.height);

  if (mRenderFrame && aIsRoot && !GetListener()->SendAsyncScrollDOMEvent(rect, size)) {
    Unused << mRenderFrame->SendAsyncScrollDOMEvent(rect, size);
  }
}

void EmbedContentController::AcknowledgeScrollUpdate(const FrameMetrics::ViewID& aScrollId, const uint32_t& aScrollGeneration)
{
  if (MessageLoop::current() != mUILoop) {
    // We have to send this message from the "UI thread" (main
    // thread).
    mUILoop->PostTask(
      FROM_HERE,
      NewRunnableMethod(this, &EmbedContentController::AcknowledgeScrollUpdate, aScrollId, aScrollGeneration));
    return;
  }
  if (mRenderFrame && !GetListener()->AcknowledgeScrollUpdate((uint32_t)aScrollId, aScrollGeneration)) {
    Unused << mRenderFrame->SendAcknowledgeScrollUpdate(aScrollId, aScrollGeneration);
  }
}

void EmbedContentController::ClearRenderFrame()
{
  mRenderFrame = nullptr;
}

/**
 * Schedules a runnable to run on the controller/UI thread at some time
 * in the future.
 */
void EmbedContentController::PostDelayedTask(Task* aTask, int aDelayMs)
{
  MessageLoop::current()->PostDelayedTask(FROM_HERE, aTask, aDelayMs);
}

EmbedLiteViewListener* const EmbedContentController::GetListener() const
{
  static FakeListener sFakeListener;
  return mRenderFrame && mRenderFrame->mView ?
         mRenderFrame->mView->GetListener() : &sFakeListener;
}

void EmbedContentController::DoRequestContentRepaint(const FrameMetrics& aFrameMetrics)
{
  if (mRenderFrame && !GetListener()->RequestContentRepaint()) {
    Unused << mRenderFrame->SendUpdateFrame(aFrameMetrics);
  }
}

nsEventStatus
EmbedContentController::ReceiveInputEvent(InputData& aEvent,
                                          mozilla::layers::ScrollableLayerGuid* aOutTargetGuid,
                                          uint64_t* aOutInputBlockId)
{

  LOGT(" has mAPZC: %p\n", mAPZC.get());

  if (!mAPZC) {
    return nsEventStatus_eIgnore;
  }

  return mAPZC->ReceiveInputEvent(aEvent, aOutTargetGuid, aOutInputBlockId);
}

void EmbedContentController::NotifyFlushComplete()
{
  printf("==================== notify flush complete\n");
}
