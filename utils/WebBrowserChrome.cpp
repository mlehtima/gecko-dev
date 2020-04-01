/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EmbedLog.h"

#include "WebBrowserChrome.h"
#include "nsIDOMWindow.h"
#include "nsIDOMDocument.h"
#include "nsIDocument.h"
#include "nsIDocShellTreeItem.h"
#include "nsIDocShell.h"
#include "nsIWebProgress.h"
#include "nsIDOMEventTarget.h"
#include "nsPIDOMWindow.h"
#include "nsNetUtil.h"
#include "nsIDOMWindowUtils.h"
#include "nsIWebNavigation.h"
#include "nsISSLStatusProvider.h"
#include "nsISecureBrowserUI.h"
#include "nsISerializationHelper.h"
#include "nsISSLStatus.h"
#include "nsIDOMEvent.h"
#include "nsIDOMHTMLLinkElement.h"
#include "nsIFocusManager.h"
#include "nsIDOMScrollAreaEvent.h"
#include "nsISerializable.h"
#include "nsIURIFixup.h"
#include "nsIEmbedBrowserChromeListener.h"
#include "nsIBaseWindow.h"
#include "ScriptSettings.h" // for AutoNoJSAPI
#include "TabChildHelper.h"

// Duplicated from EventNameList.h
#define MOZ_MozAfterPaint "MozAfterPaint"
#define MOZ_scroll "scroll"
#define MOZ_pagehide "pagehide"
#define MOZ_MozScrolledAreaChanged "MozScrolledAreaChanged"


WebBrowserChrome::WebBrowserChrome(nsIEmbedBrowserChromeListener* aListener)
  : mChromeFlags(0)
  , mIsModal(false)
  , mIsVisible(false)
  , mHandlerAdded(false)
  , mTotalRequests(0)
  , mFinishedRequests(0)
  , mLocationHasChanged(false)
  , mFirstPaint(false)
  , mScrollOffset(0,0)
  , mListener(aListener)
{
  LOGT();
}

WebBrowserChrome::~WebBrowserChrome()
{
  LOGT();
}

NS_IMPL_ISUPPORTS(WebBrowserChrome,
                  nsIWebBrowserChrome,
                  nsIWebBrowserChrome2,
                  nsIWebBrowserChromeFocus,
                  nsIInterfaceRequestor,
                  nsIEmbeddingSiteWindow,
                  nsIWebProgressListener,
                  nsISupportsWeakReference)

NS_IMETHODIMP WebBrowserChrome::GetInterface(const nsIID& aIID, void** aInstancePtr)
{
  NS_ENSURE_ARG_POINTER(aInstancePtr);

  if (aIID.Equals(NS_GET_IID(nsITabChild))) {
    nsCOMPtr<nsITabChild> tabChildHelper;
    tabChildHelper = mHelper;
    tabChildHelper.forget(aInstancePtr);
    return NS_OK;
  }

  *aInstancePtr = 0;
  if (aIID.Equals(NS_GET_IID(nsIDOMWindow))) {
    if (!mWebBrowser) {
      return NS_ERROR_NOT_INITIALIZED;
    }

    return mWebBrowser->GetContentDOMWindow((mozIDOMWindowProxy**)aInstancePtr);
  }
  return QueryInterface(aIID, aInstancePtr);
}

NS_IMETHODIMP WebBrowserChrome::SetStatus(uint32_t aStatusType, const char16_t* aStatus)
{
  return SetStatusWithContext(aStatusType,
      aStatus ? static_cast<const nsString &>(nsDependentString(aStatus))
              : EmptyString(),
      nullptr);
}

NS_IMETHODIMP WebBrowserChrome::SetStatusWithContext(uint32_t aStatusType,
                                                     const nsAString& aStatusText,
                                                     nsISupports* aStatusContext)
{
  LOGNI();
  return NS_OK;
}

NS_IMETHODIMP WebBrowserChrome::GetWebBrowser(nsIWebBrowser * *aWebBrowser)
{
  NS_ENSURE_ARG_POINTER(aWebBrowser);
  *aWebBrowser = mWebBrowser;
  NS_IF_ADDREF(*aWebBrowser);
  return NS_OK;
}

NS_IMETHODIMP WebBrowserChrome::SetWebBrowser(nsIWebBrowser* aWebBrowser)
{
  mWebBrowser = aWebBrowser;
  SetEventHandler();
  return NS_OK;
}

NS_IMETHODIMP WebBrowserChrome::GetChromeFlags(uint32_t* aChromeFlags)
{
  *aChromeFlags = mChromeFlags;
  return NS_OK;
}

NS_IMETHODIMP WebBrowserChrome::SetChromeFlags(uint32_t aChromeFlags)
{
  mChromeFlags = aChromeFlags;
  return NS_OK;
}

NS_IMETHODIMP WebBrowserChrome::DestroyBrowserWindow()
{
  LOGT();

  if (mIsModal) {
    ExitModalEventLoop(NS_OK);
  }

  mListener->OnWindowCloseRequested();

  return NS_OK;
}

NS_IMETHODIMP WebBrowserChrome::SizeBrowserTo(int32_t aCX, int32_t aCY)
{
  LOGNI("sz[%i,%i]\n", aCX, aCY);
  return NS_OK;
}

NS_IMETHODIMP WebBrowserChrome::ShowAsModal()
{
  LOGNI();
  return NS_OK;
}

NS_IMETHODIMP WebBrowserChrome::IsWindowModal(bool* _retval)
{
  NS_ENSURE_ARG_POINTER(_retval);
  *_retval = mIsModal;
  return NS_OK;
}

NS_IMETHODIMP WebBrowserChrome::ExitModalEventLoop(nsresult aStatus)
{
  LOGNI("status: %x", aStatus);
  return NS_OK;
}

// ----- Progress Listener -----

//*****************************************************************************
// WebBrowserChrome::nsIWebProgressListener
//*****************************************************************************

NS_IMETHODIMP
WebBrowserChrome::OnProgressChange(nsIWebProgress* progress, nsIRequest* request,
                                   int32_t curSelfProgress, int32_t maxSelfProgress,
                                   int32_t curTotalProgress, int32_t maxTotalProgress)
{
  NS_ENSURE_TRUE(mListener, NS_ERROR_FAILURE);
  // Filter optimization: Don't send garbage
  if (curTotalProgress > maxTotalProgress || maxTotalProgress <= 0) {
    return NS_OK;
  }

  // Filter optimization: Are we sending "request completions" as "progress changes"
  if (mTotalRequests > 1 && request) {
    return NS_OK;
  }

  nsCOMPtr<nsPIDOMWindowOuter> window = do_GetInterface(mWebBrowser);
  mozilla::dom::AutoNoJSAPI nojsapi;
  nsCOMPtr<nsIDOMWindowUtils> utils = do_GetInterface(window);
  if (!utils) {
    NS_WARNING("window Utils are null");
    return NS_OK;
  }
  uint64_t currentInnerWindowID = 0;
  utils->GetCurrentInnerWindowID(&currentInnerWindowID);

  float progFrac = (float)maxTotalProgress / 100.0f;
  int sprogress = progFrac ? (float)curTotalProgress / progFrac : 0;
  mListener->OnLoadProgress(sprogress, curTotalProgress, maxTotalProgress);

  return NS_OK;
}

NS_IMETHODIMP
WebBrowserChrome::OnStateChange(nsIWebProgress* progress, nsIRequest* request,
                                uint32_t progressStateFlags, nsresult status)
{
  NS_ENSURE_TRUE(mListener, NS_ERROR_FAILURE);
  nsCOMPtr<mozIDOMWindowProxy> docWin = do_GetInterface(mWebBrowser);
  nsCOMPtr<mozIDOMWindowProxy> progWin;
  progress->GetDOMWindow(getter_AddRefs(progWin));
  if (progWin != docWin) {
    return NS_OK;
  }

  nsCOMPtr<nsPIDOMWindowOuter> window = do_GetInterface(mWebBrowser);
  mozilla::dom::AutoNoJSAPI nojsapi;
  nsCOMPtr<nsIDOMWindowUtils> utils = do_GetInterface(window);
  if (!utils) {
    NS_WARNING("window Utils are null");
    return NS_OK;
  }
  uint64_t currentInnerWindowID = 0;
  utils->GetCurrentInnerWindowID(&currentInnerWindowID);

  if (progressStateFlags & nsIWebProgressListener::STATE_START) {
    if (progressStateFlags & nsIWebProgressListener::STATE_IS_NETWORK) {
      // Reset filter members
      mTotalRequests = mFinishedRequests = 0;
    }
    if (progressStateFlags & nsIWebProgressListener::STATE_IS_REQUEST)
      // Filter optimization: If we have more than one request, show progress
      //based on requests completing, not on percent loaded of each request
    {
      ++mTotalRequests;
    }
  } else if (progressStateFlags & nsIWebProgressListener::STATE_STOP) {
    if (progressStateFlags & nsIWebProgressListener::STATE_IS_REQUEST) {
      // Filter optimization: Request has completed, so send a "progress change"
      // Note: aRequest is null
      ++mFinishedRequests;
      OnProgressChange(progress, nullptr, 0, 0, mFinishedRequests, mTotalRequests);
    }
  }

  if (progressStateFlags & nsIWebProgressListener::STATE_START && progressStateFlags & nsIWebProgressListener::STATE_IS_DOCUMENT) {
    mListener->OnLoadStarted(mLastLocation.get());
  }
  if (progressStateFlags & nsIWebProgressListener::STATE_STOP && progressStateFlags & nsIWebProgressListener::STATE_IS_DOCUMENT) {
    mListener->OnLoadFinished();
  }
  if (progressStateFlags & nsIWebProgressListener::STATE_REDIRECTING) {
    mListener->OnLoadRedirect();
  }

  return NS_OK;
}

NS_IMETHODIMP
WebBrowserChrome::OnLocationChange(nsIWebProgress* aWebProgress,
                                   nsIRequest* aRequest,
                                   nsIURI* location,
                                   uint32_t aFlags)
{
  NS_ENSURE_TRUE(mListener, NS_ERROR_FAILURE);
  nsCOMPtr<mozIDOMWindowProxy> docWin = do_GetInterface(mWebBrowser);
  nsCOMPtr<mozIDOMWindowProxy> progWin;
  aWebProgress->GetDOMWindow(getter_AddRefs(progWin));
  if (progWin != docWin) {
    return NS_OK;
  }

  nsCString spec;
  if (location) {
    nsCOMPtr<nsIURIFixup> fixup(do_GetService("@mozilla.org/docshell/urifixup;1"));
    if (fixup) {
        nsCOMPtr<nsIURI> tmpuri;
        nsresult rv = fixup->CreateExposableURI(location, getter_AddRefs(tmpuri));
        if (NS_SUCCEEDED(rv) && tmpuri) {
            tmpuri->GetSpec(spec);
        } else {
            location->GetSpec(spec);
        }
    } else {
        location->GetSpec(spec);
    }
  }
  nsCString slocation(spec);
  int32_t i = slocation.RFind("#");
  if (i != kNotFound) {
    slocation.SetLength(i);
  }

  nsCOMPtr<nsIDOMDocument> ctDoc;
  mHelper->WebNavigation()->GetDocument(getter_AddRefs(ctDoc));
  nsString charset;
  ctDoc->GetCharacterSet(charset);

  nsCOMPtr<nsPIDOMWindowOuter> window = do_GetInterface(mWebBrowser);
  mozilla::dom::AutoNoJSAPI nojsapi;
  nsCOMPtr<nsIDOMWindowUtils> utils = do_GetInterface(window);
  if (!utils) {
    NS_WARNING("window Utils are null");
    return NS_OK;
  }

  uint64_t currentInnerWindowID = 0;
  utils->GetCurrentInnerWindowID(&currentInnerWindowID);

  nsString docURI;
  ctDoc->GetDocumentURI(docURI);

  bool canGoBack = false, canGoForward = false;
  nsCOMPtr<nsIWebNavigation> navigation = do_GetInterface(mWebBrowser);
  navigation->GetCanGoBack(&canGoBack);
  navigation->GetCanGoForward(&canGoForward);

  bool isSameDocument = aFlags & nsIWebProgressListener::LOCATION_CHANGE_SAME_DOCUMENT;

  mListener->OnLocationChanged(spec.get(), canGoBack, canGoForward, isSameDocument);

  // Keep track of hash changes
  mLocationHasChanged = slocation.Equals(mLastLocation);
  mLastLocation = slocation;
  mFirstPaint = false;

  nsCOMPtr<nsPIDOMWindowOuter> pidomWindow = do_QueryInterface(docWin);
  nsCOMPtr<nsIDOMEventTarget> target = do_QueryInterface(pidomWindow->GetChromeEventHandler());
  target->AddEventListener(NS_LITERAL_STRING(MOZ_MozAfterPaint), this, PR_FALSE);

  return NS_OK;
}

NS_IMETHODIMP
WebBrowserChrome::OnStatusChange(nsIWebProgress* aWebProgress,
                                 nsIRequest* aRequest,
                                 nsresult aStatus,
                                 const char16_t* aMessage)
{
  LOGNI();
  return NS_OK;
}

NS_IMETHODIMP
WebBrowserChrome::OnSecurityChange(nsIWebProgress* aWebProgress,
                                   nsIRequest* aRequest,
                                   uint32_t state)
{
  NS_ENSURE_TRUE(mListener, NS_ERROR_FAILURE);
  nsCOMPtr<mozIDOMWindowProxy> docWin = do_GetInterface(mWebBrowser);
  nsCOMPtr<mozIDOMWindowProxy> progWin;
  aWebProgress->GetDOMWindow(getter_AddRefs(progWin));
  if (progWin != docWin) {
    return NS_OK;
  }

  nsCOMPtr<nsPIDOMWindowOuter> window = do_GetInterface(mWebBrowser);
  mozilla::dom::AutoNoJSAPI nojsapi;
  nsCOMPtr<nsIDOMWindowUtils> utils = do_GetInterface(window);
  if (!utils) {
    NS_WARNING("window Utils are null");
    return NS_OK;
  }
  uint64_t currentInnerWindowID = 0;
  utils->GetCurrentInnerWindowID(&currentInnerWindowID);

  nsCString serSSLStatus;
  nsCOMPtr<nsIDocShell> docShell = do_GetInterface(mWebBrowser);
  nsCOMPtr<nsISecureBrowserUI> secureUI;
  docShell->GetSecurityUI(getter_AddRefs(secureUI));
  nsCOMPtr<nsISSLStatusProvider> sslProvider = do_QueryInterface(secureUI);
  nsCOMPtr<nsISSLStatus> sslStatus;
  sslProvider->GetSSLStatus(getter_AddRefs(sslStatus));
  if (sslStatus) {
    nsCOMPtr<nsISerializationHelper> serialHelper = do_GetService("@mozilla.org/network/serialization-helper;1");
    nsCOMPtr<nsISerializable> serializableStatus = do_QueryInterface(sslStatus);
    serialHelper->SerializeToString(serializableStatus, serSSLStatus);
  }
  mListener->OnSecurityChanged(serSSLStatus.get(), state);

  return NS_OK;
}

//*****************************************************************************
// WebBrowserChrome::nsIDomEventListener
//*****************************************************************************

NS_IMETHODIMP
WebBrowserChrome::HandleEvent(nsIDOMEvent* aEvent)
{
  NS_ENSURE_TRUE(mListener, NS_ERROR_FAILURE);

  nsString type;
  if (aEvent) {
    aEvent->GetType(type);
  }

  LOGT("Event:'%s'", NS_ConvertUTF16toUTF8(type).get());

  nsCOMPtr<mozIDOMWindowProxy> docWin = do_GetInterface(mWebBrowser);
  nsCOMPtr<nsPIDOMWindowOuter> window = do_GetInterface(mWebBrowser);
  mozilla::dom::AutoNoJSAPI nojsapi;
  nsCOMPtr<nsIDOMWindowUtils> utils = do_GetInterface(window);
  if (type.EqualsLiteral(MOZ_MozScrolledAreaChanged)) {
    nsCOMPtr<nsIDOMEventTarget> origTarget;
    aEvent->GetOriginalTarget(getter_AddRefs(origTarget));
    nsCOMPtr<nsIDOMDocument> ctDoc = do_QueryInterface(origTarget);
    nsCOMPtr<mozIDOMWindowProxy> targetWin;
    ctDoc->GetDefaultView(getter_AddRefs(targetWin));
    if (targetWin != docWin) {
      return NS_OK; // We are only interested in root scroll pane changes
    }

    // Adjust width and height from the incoming event properties so that we
    // ignore changes to width and height contributed by growth in page
    // quadrants other than x > 0 && y > 0.
    nsIntPoint scrollOffset = GetScrollOffset(docWin);
    nsCOMPtr<nsIDOMScrollAreaEvent> scrollEvent = do_QueryInterface(aEvent);
#if 0
    float evX, evY, evW, evH;
    scrollEvent->GetX(&evX);
    scrollEvent->GetY(&evY);
    scrollEvent->GetWidth(&evW);
    scrollEvent->GetHeight(&evH);
    float x = evX + scrollOffset.x;
    float y = evY + scrollOffset.y;
    uint32_t width = evW + (x < 0 ? x : 0);
    uint32_t height = evH + (y < 0 ? y : 0);
    mListener->OnScrolledAreaChanged(width, height);
#endif

    nsCOMPtr<nsIDOMEventTarget> target = do_QueryInterface(window->GetChromeEventHandler());
    target->AddEventListener(NS_LITERAL_STRING(MOZ_MozAfterPaint), this, PR_FALSE);
  } else if (type.EqualsLiteral(MOZ_pagehide)) {
    mScrollOffset = nsIntPoint();
  } else if (type.EqualsLiteral(MOZ_MozAfterPaint)) {
    nsCOMPtr<nsPIDOMWindowOuter> pidomWindow = do_QueryInterface(docWin);
    nsCOMPtr<nsIDOMEventTarget> target = do_QueryInterface(pidomWindow->GetChromeEventHandler());
    target->RemoveEventListener(NS_LITERAL_STRING(MOZ_MozAfterPaint), this, PR_FALSE);
    if (mFirstPaint) {
      mListener->OnUpdateDisplayPort();
      return NS_OK;
    }
    mFirstPaint = true;
    nsIntPoint offset = GetScrollOffset(docWin);
    mListener->OnFirstPaint(offset.x, offset.y);
  } else if (type.EqualsLiteral(MOZ_scroll)) {
    nsCOMPtr<nsIDOMEventTarget> target;
    aEvent->GetTarget(getter_AddRefs(target));
    nsCOMPtr<nsIDOMDocument> eventDoc = do_QueryInterface(target);
    nsCOMPtr<nsIDOMDocument> ctDoc;
    mHelper->WebNavigation()->GetDocument(getter_AddRefs(ctDoc));
    if (eventDoc != ctDoc) {
      return NS_OK;
    }
    SendScroll();
  }

  return NS_OK;
}

// TOOLS
nsIntPoint
WebBrowserChrome::GetScrollOffset(mozIDOMWindowProxy* aWindow)
{
  mozilla::dom::AutoNoJSAPI nojsapi;
  nsCOMPtr<nsIDOMWindowUtils> utils = do_GetInterface(aWindow);
  nsIntPoint scrollOffset;
  utils->GetScrollXY(PR_FALSE, &scrollOffset.x, &scrollOffset.y);
  return scrollOffset;
}

nsIntPoint
WebBrowserChrome::GetScrollOffsetForElement(nsIDOMElement* aElement)
{
  nsCOMPtr<nsIDOMDocument> ownerDoc;
  aElement->GetOwnerDocument(getter_AddRefs(ownerDoc));
  nsCOMPtr<mozIDOMWindowProxy> domWindow;
  nsCOMPtr<nsIDOMNode> parentNode;
  aElement->GetParentNode(getter_AddRefs(parentNode));
  if (parentNode == ownerDoc) {
    ownerDoc->GetDefaultView(getter_AddRefs(domWindow));
    return GetScrollOffset(domWindow);
  }

  nsIntPoint scrollOffset;
  aElement->GetScrollLeft(&scrollOffset.x);
  aElement->GetScrollTop(&scrollOffset.y);
  return scrollOffset;
}

void
WebBrowserChrome::SetScrollOffsetForElement(nsIDOMElement* aElement, int32_t aLeft, int32_t aTop)
{
  nsCOMPtr<nsIDOMDocument> ownerDoc;
  aElement->GetOwnerDocument(getter_AddRefs(ownerDoc));
  nsCOMPtr<nsIDOMNode> parentNode;
  aElement->GetParentNode(getter_AddRefs(parentNode));
  if (parentNode == ownerDoc) {
    nsCOMPtr<mozIDOMWindowProxy> pwindow = do_GetInterface(mWebBrowser);
    nsGlobalWindow* window = nsGlobalWindow::Cast(pwindow);
    window->ScrollTo(aLeft, aTop);
  } else {
    aElement->SetScrollLeft(aLeft);
    aElement->SetScrollTop(aTop);
  }
}

void
WebBrowserChrome::SendScroll()
{
  NS_ENSURE_TRUE(mListener, );
  nsCOMPtr<mozIDOMWindowProxy> window = do_GetInterface(mWebBrowser);
  nsIntPoint offset = GetScrollOffset(window);
  if (mScrollOffset.x == offset.x && mScrollOffset.y == offset.y) {
    return;
  }
  mScrollOffset = offset;
  mListener->OnScrollChanged(offset.x, offset.y);
}


// ----- Embedding Site Window
NS_IMETHODIMP WebBrowserChrome::SetDimensions(uint32_t aFlags,
                                              int32_t aX, int32_t aY,
                                              int32_t aCx, int32_t aCy)
{
  // TODO: currently only does size
  LOGNI("flags:%u, pt[%i,%i] sz[%i,%i]\n", aFlags, aX, aY, aCx, aCy);
  return NS_OK;
}

NS_IMETHODIMP WebBrowserChrome::GetDimensions(uint32_t aFlags,
                                              int32_t* aX, int32_t* aY,
                                              int32_t* aCx, int32_t* aCy)
{
  LOGNI("GetView dimensitions");
  /*
      QMozEmbedQGVWidget* view = pMozView->View();
      if (aFlags & nsIEmbeddingSiteWindow::DIM_FLAGS_POSITION) {
          QPoint pt(view->GetScreenPos());
          *aX = pt.x();
          *aY = pt.y();
      }
      if (aFlags & nsIEmbeddingSiteWindow::DIM_FLAGS_SIZE_INNER) {
          *aCx = view->geometry().width();
          *aCy = view->geometry().height();
      } else if (aFlags & nsIEmbeddingSiteWindow::DIM_FLAGS_SIZE_OUTER) {
          *aCx = view->geometry().width();
          *aCy = view->geometry().height();
      }
  */

  return NS_OK;
}

NS_IMETHODIMP WebBrowserChrome::SetFocus()
{
  LOGNI();
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP WebBrowserChrome::GetVisibility(bool* aVisibility)
{
  *aVisibility = mIsVisible;

  return NS_OK;
}

NS_IMETHODIMP WebBrowserChrome::SetVisibility(bool aVisibility)
{
  mIsVisible = aVisibility;
  return NS_OK;
}

NS_IMETHODIMP WebBrowserChrome::GetTitle(char16_t** aTitle)
{
  NS_ENSURE_ARG_POINTER(aTitle);
  *aTitle = ToNewUnicode(mTitle);
  return NS_OK;
}

NS_IMETHODIMP WebBrowserChrome::SetTitle(const char16_t* aTitle)
{
  // Store local title
  mTitle.Assign(nsDependentString(aTitle));
  if (mListener) {
      mListener->OnTitleChanged(mTitle.get());
  }
  return NS_OK;
}

NS_IMETHODIMP WebBrowserChrome::GetSiteWindow(void * *aSiteWindow)
{
  NS_ENSURE_ARG_POINTER(aSiteWindow);
  LOGNI();
  return NS_OK;
}

/* void blur (); */
NS_IMETHODIMP WebBrowserChrome::Blur()
{
  LOGNI();
  return NS_ERROR_NOT_IMPLEMENTED;
}

// ----- WebBrowser Chrome Focus

NS_IMETHODIMP WebBrowserChrome::FocusNextElement(bool aForDocumentNavigation)
{
  LOGNI();
  return NS_OK;
}

NS_IMETHODIMP WebBrowserChrome::FocusPrevElement(bool aForDocumentNavigation)
{
  LOGNI();
  return NS_OK;
}

void WebBrowserChrome::SetEventHandler()
{
  if (mHandlerAdded) {
    NS_ERROR("Handler was already added");
    return;
  }
  mHandlerAdded = true;
  nsCOMPtr<nsIDocShell> docShell = do_GetInterface(mWebBrowser);
  NS_ENSURE_TRUE(docShell, );
  nsCOMPtr<nsIWebProgress> wp(do_GetInterface(docShell));
  NS_ENSURE_TRUE(wp, );
  nsCOMPtr<nsIWebProgressListener> listener(static_cast<nsIWebProgressListener*>(this));
  nsCOMPtr<nsIWeakReference> thisListener(do_GetWeakReference(listener));
  nsCOMPtr<nsIWebProgressListener> wpl = do_QueryReferent(thisListener);
  NS_ENSURE_TRUE(wpl, );
  wp->AddProgressListener(wpl,
                          nsIWebProgress::NOTIFY_SECURITY |
                          nsIWebProgress::NOTIFY_LOCATION |
                          nsIWebProgress::NOTIFY_STATE_NETWORK |
                          nsIWebProgress::NOTIFY_STATE_REQUEST |
                          nsIWebProgress::NOTIFY_STATE_DOCUMENT |
                          nsIWebProgress::NOTIFY_PROGRESS);

  nsCOMPtr<nsPIDOMWindowOuter> pidomWindow = do_QueryInterface(mWebBrowser);
  NS_ENSURE_TRUE(pidomWindow, );
  nsCOMPtr<nsIDOMEventTarget> target = do_QueryInterface(pidomWindow->GetChromeEventHandler());
  NS_ENSURE_TRUE(target, );
  target->AddEventListener(NS_LITERAL_STRING(MOZ_MozScrolledAreaChanged), this, PR_FALSE);
  target->AddEventListener(NS_LITERAL_STRING(MOZ_scroll), this, PR_FALSE);
  target->AddEventListener(NS_LITERAL_STRING(MOZ_pagehide), this, PR_FALSE);
}

void WebBrowserChrome::RemoveEventHandler()
{
  if (!mHandlerAdded) {
    NS_ERROR("Handler was not added");
    return;
  }

  mListener = nullptr;
  mHandlerAdded = false;
  nsCOMPtr<nsPIDOMWindowOuter> pidomWindow = do_QueryInterface(mWebBrowser);
  NS_ENSURE_TRUE(pidomWindow, );
  nsCOMPtr<nsIDOMEventTarget> target = do_QueryInterface(pidomWindow->GetChromeEventHandler());
  NS_ENSURE_TRUE(target, );
  target->RemoveEventListener(NS_LITERAL_STRING(MOZ_MozScrolledAreaChanged), this, PR_FALSE);
  target->RemoveEventListener(NS_LITERAL_STRING(MOZ_pagehide), this, PR_FALSE);
  target->RemoveEventListener(NS_LITERAL_STRING(MOZ_scroll), this, PR_FALSE);
  target->RemoveEventListener(NS_LITERAL_STRING(MOZ_MozAfterPaint), this, PR_FALSE);
}

void WebBrowserChrome::SetTabChildHelper(TabChildHelper* aHelper)
{
  NS_ASSERTION(aHelper, "TabChildHelper can't be unset");
  NS_ASSERTION(!mHelper, "TabChildHelper can be set only once");

  mHelper = aHelper;
}
