#pragma once

#include <memory>
#include <string>

class UnderlayMonitor
{
  public:
    /**
     * @brief Monitor an underlay window and keep overlay on top of it
     * @param overlay window to keep overlaid
     */
    explicit UnderlayMonitor(HWND overlay);

    /**
     * @brief Destructor
     */
    ~UnderlayMonitor();

    /**
     * @brief Monitor process window as an underlay
     * @param processName name of windowed process to monitor
     * TODO - do we care about errors/return codes here?
     * @return true on success
     */
    bool StartMonitor(const std::wstring processName);

    /**
     * @brief Stop underlay monitor if running
     */
    void StopMonitor();

    /**
     * @brief Return name of process being monitored as an underlay
     */
    std::wstring CurrentProcessName() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl; ///< The pimpl, hides most implementation detail.
};
