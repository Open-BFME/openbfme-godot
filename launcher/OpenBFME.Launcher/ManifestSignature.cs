using System.Security.Cryptography;

namespace OpenBFME.Launcher;

public static class ManifestSignature
{
    private const string ProductionPublicKey = """
    -----BEGIN PUBLIC KEY-----
    MIIBojANBgkqhkiG9w0BAQEFAAOCAY8AMIIBigKCAYEAoSmgwzaiW3dD1My/+oUV
    MNDmCW8Vgq9GmQC7kifo3356aK8YrsKokqqzV3nfw9ImYaQ8eS41aTgYA9E7Tnk5
    a+J4SAConVRVLBIDc8Bi6bjnIEpUhQWY5SJNDPm611Kzm9hVa7X+gUnihIEZSCJp
    bviFaqeoUt70OHEO9XSsBLf6JgUeR0+ySipMsdm1mNTDGQsuIKc89OzI7aKX4mKx
    dbD3RtO2IKSnOvfAxkJJXlDlZva285rHmTkpksZI9LKf+3bAll7qh5L4Gw4xvJri
    Pd3u4AmzwFPkSkWVMWR89RPe3m2D7nSCSQAjhcNJR3Kei+yaEQtCX28BDqlTyoiV
    yWr9UBxKV5f7YS5akYDn1f9s00f/wl/sJ28dyrovDgMBtTz0DhsvmHyu1nNkXSTG
    cpZb8JPeeM23Bog3YiNqftDVlYPDuc2rjo/t9zDQRSB08d4dGcDtPWEPBOiXAL7k
    knV5SoMsSO+q0n14uFdIZUqd1CN+ofBA1u5bYnoKupaJAgMBAAE=
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
