// Ghidra headless script: print direct references and containing functions for
// one or more image-relative virtual addresses supplied as hexadecimal args.
// @category CSX.RenderMap

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class GhidraRenderMapXrefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length == 0) {
            printerr("Supply one or more image-relative RVAs in hexadecimal.");
            return;
        }

        println("PROGRAM " + currentProgram.getName());
        println("IMAGE_BASE " + currentProgram.getImageBase());

        for (String arg : args) {
            long rva = Long.parseUnsignedLong(arg.replaceFirst("^(?i)0x", ""), 16);
            Address target = currentProgram.getImageBase().add(rva);
            Function targetFunction = currentProgram.getFunctionManager().getFunctionContaining(target);
            println("\nTARGET RVA=0x" + Long.toHexString(rva) + " ADDRESS=" + target +
                " FUNCTION=" + describe(targetFunction));

            ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(target);
            int count = 0;
            while (references.hasNext()) {
                Reference reference = references.next();
                Function owner = currentProgram.getFunctionManager().getFunctionContaining(reference.getFromAddress());
                println("REF FROM=" + reference.getFromAddress() + " TYPE=" + reference.getReferenceType() +
                    " OWNER=" + describe(owner));
                count++;
            }
            println("REFERENCE_COUNT " + count);
        }
    }

    private String describe(Function function) {
        if (function == null) {
            return "<none>";
        }
        return function.getName() + "@" + function.getEntryPoint();
    }
}
