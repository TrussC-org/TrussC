#pragma once
#include <stdexcept>

// The aggregate test drives the shared routine through the native exception
// boundary. review-test supplies the actual main/Linux event and tick entries.
template<class Probe, class Event, class Tick, class Check>
void checkResizeDelivery(tc::Window& window, sapp_tc_window_metadata& metadata,
                         const std::shared_ptr<Probe>& app, Event event, Tick tick, Check check) {
    metadata.dpi_scale = 2;
    auto extent = [](int pixels) { return tc::internal::pixelPerfectMode() ? pixels : pixels / 2; };
    auto observe = [&](int pixels) {
        metadata.fb_width = metadata.fb_height = pixels;
        event();
    };
    auto matches = [&](int pixels) {
        return app->getWidth() == extent(pixels) && app->getHeight() == extent(pixels);
    };
    observe(100);
    int notifications = 0;
    auto nested = window.events().windowResized.listen([&](tc::ResizeEventArgs& e) {
        ++notifications;
        if (e.width == extent(150)) observe(200);
    }, tc::EventPriority::BeforeApp);
    const auto beforeNested = app->sizes;
    observe(150);
    check("nested newer resize wins before outer native callback returns",
          matches(200) && trussc::internal::WindowRequestAccess::context(window).fbWidth == 200 && notifications == 2 && app->sizes == beforeNested + 2);
    tick();
    check("next tick retains newest App extent and deduplicates it",
          matches(200) && notifications == 2 && app->sizes == beforeNested + 2);
    nested.disconnect();

    int entryNotifications = 0, firstExtent = 0, lastExtent = 0;
    auto feedback = window.events().windowResized.listen([&](tc::ResizeEventArgs& e) {
        if (++entryNotifications == 1) firstExtent = e.width;
        lastExtent = e.width;
        // Stop feedback even without the production bound: mutations must
        // fail assertions and return, rather than hang in the native entry.
        if (entryNotifications < 12) observe(e.width == extent(150) ? 200 : 150);
    }, tc::EventPriority::BeforeApp);
    auto checkFeedbackEntry = [&](int pendingPixels, int beforeSizes) {
        check("feedback returns within three deliveries and keeps making progress",
              entryNotifications > 0 && entryNotifications <= 3 && app->sizes == beforeSizes + entryNotifications);
        check("feedback entry starts with the latest outstanding native extent", firstExtent == extent(pendingPixels));
        check("feedback leaves App at a delivered size the native window actually had",
              (matches(150) || matches(200)) && app->getWidth() == lastExtent && app->getHeight() == lastExtent);
        check("feedback retains the newest observation for another tick",
              trussc::internal::WindowRequestAccess::context(window).fbWidth == (lastExtent == extent(150) ? 200 : 150) &&
              trussc::internal::WindowRequestAccess::context(window).fbHeight == trussc::internal::WindowRequestAccess::context(window).fbWidth);
    };
    const auto beforeFeedback = app->sizes;
    observe(150);
    checkFeedbackEntry(150, beforeFeedback);
    for (int i = 0; i != 4; ++i) {
        const int pendingPixels = trussc::internal::WindowRequestAccess::context(window).fbWidth;
        const auto beforeTick = app->sizes;
        entryNotifications = firstExtent = lastExtent = 0;
        tick();
        checkFeedbackEntry(pendingPixels, beforeTick);
    }
    feedback.disconnect();
    const int pendingPixels = trussc::internal::WindowRequestAccess::context(window).fbWidth;
    const auto beforeCatchup = app->sizes;
    tick();
    check("feedback disconnect lets the next tick deliver the outstanding extent",
          matches(pendingPixels) && app->sizes == beforeCatchup + 1);
    tick();
    check("feedback catchup deduplicates on following ticks", matches(pendingPixels) && app->sizes == beforeCatchup + 1);
    observe(200);

    // Preserve the first review's argument-edit collision fix.
    notifications = 0;
    auto collision = window.events().windowResized.listen([&](tc::ResizeEventArgs& e) {
        ++notifications; e.width = e.height = extent(400);
    }, tc::EventPriority::BeforeApp);
    observe(375);
    check("listener can edit App to a future native size", notifications == 1 && matches(400));
    observe(400);
    check("real change still delivers when edited App already matches", notifications == 2 && matches(400));
    event(); tick();
    check("identical native events and ticks still deduplicate", notifications == 2 && matches(400));

    int attempts = 0;
    auto failure = window.events().windowResized.listen([&](tc::ResizeEventArgs&) {
        ++attempts;
        throw std::runtime_error("terminal resize callback failure");
    }, tc::EventPriority::BeforeApp);
    observe(450);
    check("throwing resize is contained and fails the Window", attempts == 1 && !window.isOpen());
    tick(); tick();
    check("failed Window never replays resize on later ticks", attempts == 1);
}
