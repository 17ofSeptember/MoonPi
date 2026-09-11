# Starter projects (0.7)

Open **Starters** in the workspace toolbar, or **Browse starter projects** in the
component library. Search the five bundled projects: LED blink, button-controlled
LED, timed sequence, BME280 temperature alarm and scripted digital pin.

Each preview lists parts, board-to-component wiring and steps to try in simulation.
Wiring rows come directly from the bundled example's edges and board pin catalogue.
Simulation requires no physical components. The guides describe the original
examples; after editing, inspect the current canvas and validate it again.

**Open starter** creates a new editable project with fresh project, board, node and
wire IDs. An existing populated or locally edited canvas requires a replacement
confirmation, with an option to export it first. Cancel keeps it intact. Opening
is one undoable edit; Undo and Redo restore whole designs. The gallery blocks
replacement while automation is running. Stop it first.

Opening never applies, runs or saves automatically. Use the visible guide to
Validate, Apply, Run, observe, and Save. The guide's identity is stored in project
extensions and returns when the project is saved and reloaded; it can be hidden.

**Saved to disk** is separate from **Applied**. The backend reports whether its
current design matches the last successfully saved or loaded project. Local edits
show **Not saved to disk** immediately. Validation and Apply do not save. This
status does not continuously detect external changes to the project file.

Gallery confirmation covers opening starters. Existing New, Import and Reload
controls retain their prior behavior; export valuable work before using them.
Undo history lasts only in the current browser session. Export downloads a copy;
Save updates the native service's project file. Closing a browser does not stop
running automation.

All example data and help are bundled into the frontend; the gallery works offline
and does not require a new API or internet service. Each bundled example is checked
with the native graph compiler in the core test suite.
