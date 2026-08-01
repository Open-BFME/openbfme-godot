using System.Security.Cryptography;

namespace OpenBFME.Launcher;

public static class ManifestSignature
{
    private const string ProductionPublicKey = """
    -----BEGIN PUBLIC KEY-----
    MIIBojANBgkqhkiG9w0BAQEFAAOCAY8AMIIBigKCAYEAvmoAtfGlwea7Dve+8d0I
    gD/z+HC+ZgnSlKQa4zs3KX+5p2NInEz0Z1RcZ7OketiqMlWHN6KVUrkbXTl+hoCT
    9Vc2Ub1dvHDLIORwXVB/7Q514/UgRvaz8jpI1PTyQ8md+vhw0O+hBeL7Gp6ZxQ2/
    97WgsSoWTLJjm+0HObNTPR03X02tNkyuU9dviqLMRGzNOvbSOL+faErH1TU79fIV
    1153iyzrNX3kxIdyp2SyqE8+hlOGRdtzDiUVKRqaZo9B3s+xsgihQlSXeg0L+3e7
    gSkVyNrUlsKqvyQ+uL9SMOUC2HxXNrGR09c+fFun/X3VaUkK6Qdsk6soWdMfuSnd
    GtZBIrOEVQyf7Ufs+KqUUuzkeaQRiBQz9Izdui71iZEJyidI+UQVMj+9NYNkmMfR
    XXaqT0iOb/S/pb+YVzsZVzxQCF7wurHMzwOnLpS46/FoEcCcAMQUJjyupR0bFeKi
    cPDMm0V/+rU0W6D5DvxeDRxlBimLC/0ADriYGtK1eKT7AgMBAAE=
    -----END PUBLIC KEY-----
    """;

    public static void Verify(ReadOnlySpan<byte> manifest, ReadOnlySpan<byte> signatureText) =>
        Verify(manifest, signatureText, ProductionPublicKey);

    internal static void Verify(
        ReadOnlySpan<byte> manifest,
        ReadOnlySpan<byte> signatureText,
        string publicKeyPem)
    {
        byte[] signature;
        try
        {
            signature = Convert.FromBase64String(
                System.Text.Encoding.ASCII.GetString(signatureText).Trim());
        }
        catch (FormatException error)
        {
            throw new InvalidDataException("Release manifest signature is not valid base64.", error);
        }
        using var rsa = RSA.Create();
        rsa.ImportFromPem(publicKeyPem);
        if (!rsa.VerifyData(manifest, signature, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1))
            throw new InvalidDataException("Release manifest signature is invalid.");
    }
}
