using PKHeX.Core;

if (args.Length != 1 || !Directory.Exists(args[0]))
    return 2;
int legal = 0, illegal = 0, refused = 0, errors = 0;
foreach (string path in Directory.EnumerateFiles(args[0]).OrderBy(p => p))
{
    Console.WriteLine($"=== {Path.GetFileName(path)} ===");
    if (path.EndsWith(".refused.txt", StringComparison.Ordinal))
    {
        refused++;
        Console.WriteLine(File.ReadAllText(path));
        continue;
    }
    if (!path.EndsWith(".pk3", StringComparison.Ordinal)) continue;
    try
    {
        var bytes = File.ReadAllBytes(path);
        if (bytes.Length != 80) throw new InvalidDataException("Not a stored PK3");
        var pk = new PK3(bytes);
        var la = new LegalityAnalysis(pk);
        if (!la.Parsed) errors++;
        else if (la.Valid) legal++;
        else illegal++;
        Console.WriteLine($"Species={pk.Species} Language={pk.Language} Parsed={la.Parsed} Valid={la.Valid}");
        Console.WriteLine(la.Report(verbose: true));
    }
    catch (Exception ex)
    {
        errors++;
        Console.WriteLine(ex);
    }
}
Console.WriteLine($"PKHeX audit: valid={legal}, invalid={illegal}, conversion refused={refused}, errors={errors}");
if (legal + illegal + refused + errors != 36 || errors != 0) return 1;
// A report, not a claim of official provenance or blanket legality.
return 0;
