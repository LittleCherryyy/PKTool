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
        Console.WriteLine($"OT bytes={Convert.ToHexString(pk.OriginalTrainerTrash)} Nickname bytes={Convert.ToHexString(pk.NicknameTrash)}");
    }
    catch (Exception ex)
    {
        errors++;
        Console.WriteLine(ex);
    }
}
Console.WriteLine($"PKHeX audit: valid={legal}, invalid={illegal}, conversion refused={refused}, errors={errors}");
// VPT038 adds the exact opt-in repaired Gen II EXP-mismatch output to the
// 36 historical language/destination fixtures. Every one must be PKHeX-valid.
if (legal != 37 || illegal != 0 || refused != 0 || errors != 0) return 1;
// This gate applies only to this regression corpus, not all possible Pokemon.
return 0;
