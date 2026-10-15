#pragma once

/**
 * @struct EditorModeState
 * @brief Struct keeping track of active editor states (e.g. fly mode vs editor UI interaction).
 */
struct EditorModeState {
    /** @brief Whether camera fly/control mode is active (either via persistent toggle or holding RMB). */
    bool flyMode = false;
    /** @brief Whether persistent fly mode is toggled on (via 'F' key or UI button). */
    bool flyToggled = false;
    /** @brief Whether the scene is currently simulating in Play Mode. */
    bool isPlaying = false;
    /** @brief Deferred play trigger. */
    bool pendingPlay = false;
    /** @brief Deferred stop trigger. */
    bool pendingStop = false;
    /** @brief Whether the editor UI overlay is enabled. */
    bool isEditorActive = true;
};
