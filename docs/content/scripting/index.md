# QOwnNotes Scripting

A QOwnNotes script is mostly a **JavaScript** file with a `qml` file extension.

!!! example "Example `hello-world.qml`"
    ```js
    import QtQml 2.0
    import QOwnNotesTypes 1.0

    Script {
        /**
         * Will be run when the scripting engine initializes
         */
        function init() {
            script.log("Hello world!");
        }
    }
    ```

You can place those QML files anywhere you like and **add use them in QOwnNotes**
by adding them in the **Scripting settings**.

!!! tip
    Take a look at the [example scripts](https://github.com/pbek/QOwnNotes/blob/develop/docs/content/scripting/examples)
    to get started fast.

In the **Scripting settings** you can also install scripts directly from the [**Script repository**](https://github.com/qownnotes/scripts).

For issues, questions or feature requests for scripts from the **Script repository** please open
an issue on the [QOwnNotes script repository issue page](https://github.com/qownnotes/scripts/issues).

!!! info
    If you want to propose a script for the **Script repository** please follow the
    instructions at [QOwnNotes script repository](https://github.com/qownnotes/scripts).

If you need access to a certain functionality in QOwnNotes or have
questions or ideas please open an issue on the [QOwnNotes issue page](https://github.com/pbek/QOwnNotes/issues).

!!! help
    For logging you can use the `script.log()` command to log to the log widget.
