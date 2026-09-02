using System.Text;

namespace OpenBfme.Host;

internal static class Program
{
    public static int Main()
    {
        Console.InputEncoding = new UTF8Encoding(encoderShouldEmitUTF8Identifier: false);
        Console.OutputEncoding = new UTF8Encoding(encoderShouldEmitUTF8Identifier: false);

        try
        {
            using var input = new StreamReader(
                Console.OpenStandardInput(), Console.InputEncoding, detectEncodingFromByteOrderMarks: true);
            using var output = new StreamWriter(Console.OpenStandardOutput(), Console.OutputEncoding)
            {
                AutoFlush = true,
                NewLine = "\n",
            };
            var session = new HostProtocolSession();
            while (session.IsRunning && input.ReadLine() is { } line)
            {
                foreach (var reply in session.HandleLine(line))
                {
                    output.WriteLine(reply);
                }
            }
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"OpenBfme.Host fatal error: {exception.Message}");
            return 1;
        }
    }
}
