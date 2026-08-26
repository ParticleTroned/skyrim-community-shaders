// SPDX-License-Identifier: GPL-3.0-or-later

import java.util.ArrayList;
import java.util.Set;

/** Normalizes Windows headless arguments before invoking GhidrAssistMCP. */
public class CSXGAMCPStartServerScript
        extends ghidrassistmcp.scripts.GAMCPStartServerScript {

    private static final Set<String> NAMED_ARGUMENTS = Set.of(
        "host",
        "port",
        "wait",
        "completion_file",
        "tool_profile"
    );

    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        ArrayList<String> normalized = new ArrayList<>();
        for (int index = 0; index < arguments.length; index++) {
            String argument = arguments[index];
            if (NAMED_ARGUMENTS.contains(argument) && index + 1 < arguments.length) {
                normalized.add(argument + "=" + arguments[++index]);
            } else {
                normalized.add(argument);
            }
        }

        setScriptArgs(normalized.toArray(String[]::new));
        super.run();
    }
}
