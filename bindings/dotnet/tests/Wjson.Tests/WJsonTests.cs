using System;
using System.IO;
using System.Linq;
using System.Text;
using Wjson;
using Xunit;

public class WJsonTests
{
    [Fact]
    public void Version_is_reported()
    {
        Assert.Matches(@"^\d+\.\d+\.\d+", WJson.Version);
    }

    [Fact]
    public void Parses_all_value_types()
    {
        var obj = Assert.IsType<JsonObject>(WJson.Parse(
            "{\"s\": \"Iv\u00e1n \U0001F600\", \"i\": -42, \"big\": 9223372036854775808, \"d\": 2.5, " +
            "\"t\": true, \"f\": false, \"n\": null, \"a\": [1, [2]], \"o\": {}}"));

        Assert.Equal("Iv\u00e1n \U0001F600", obj["s"]);
        Assert.Equal(-42L, obj["i"]);
        Assert.Equal(9223372036854775808.0, obj["big"]); // too big for long: becomes double
        Assert.Equal(2.5, obj["d"]);
        Assert.Equal(true, obj["t"]);
        Assert.Equal(false, obj["f"]);
        Assert.True(obj.Contains("n"));
        Assert.Null(obj["n"]);
        var a = Assert.IsType<JsonArray>(obj["a"]);
        Assert.Equal(1L, a[0]);
        Assert.Equal(2L, Assert.IsType<JsonArray>(a[1])[0]);
        Assert.Empty(Assert.IsType<JsonObject>(obj["o"]));
    }

    [Fact]
    public void Objects_keep_document_order_and_last_duplicate_wins()
    {
        var obj = Assert.IsType<JsonObject>(WJson.Parse("{\"z\": 1, \"a\": 2, \"m\": 3, \"a\": 4}"));
        Assert.Equal(new[] { "z", "a", "m" }, obj.Keys.Cast<string>().ToArray());
        Assert.Equal(4L, obj["a"]);
        Assert.True(obj.TryGetValue("m", out var m));
        Assert.Equal(3L, m);
        Assert.False(obj.TryGetValue("missing", out _));
        Assert.False(obj.Contains("A")); // case-sensitive
    }

    [Fact]
    public void Errors_have_position_and_code()
    {
        var ex = Assert.Throws<JsonParseException>(() => WJson.Parse("{\n  \"a\": [1, 2,]\n}"));
        Assert.Equal(JsonErrorCode.UnexpectedCharacter, ex.Error.Code);
        Assert.Equal(2, ex.Error.Line);
        Assert.Equal(14, ex.Error.Column);
        Assert.Equal("2:14: unexpected character", ex.Message);
        Assert.IsAssignableFrom<FormatException>(ex);
    }

    [Fact]
    public void Jsonc_and_limits()
    {
        const string jsonc = "{ // comment\n \"a\": [1, 2,], /* block */ }";
        Assert.Throws<JsonParseException>(() => WJson.Parse(jsonc));
        var obj = Assert.IsType<JsonObject>(WJson.Parse(jsonc, ParseOptions.Jsonc));
        Assert.Equal(2, Assert.IsType<JsonArray>(obj["a"]).Count);

        var shallow = new ParseOptions { MaxDepth = 2 };
        Assert.False(WJson.TryValidate("[[[1]]]", out var error, shallow));
        Assert.Equal(JsonErrorCode.DepthLimitExceeded, error!.Code);

        var shortStrings = new ParseOptions { MaxStringLength = 3 };
        Assert.True(WJson.TryValidate("[\"abc\"]", out _, shortStrings));
        Assert.False(WJson.TryValidate("[\"abcd\"]", out error, shortStrings));
        Assert.Equal(JsonErrorCode.StringTooLong, error!.Code);
    }

    [Fact]
    public void Format_and_minify()
    {
        Assert.Equal("{\n  \"a\": [\n    1\n  ]\n}", WJson.Format("{\"a\":[1]}"));
        Assert.Equal("{\"a\":[1]}", WJson.Minify(" { \"a\" : [ 1 ] } "));
        Assert.Equal("[\"\\u00e9\"]", WJson.Minify("[\"\u00e9\"]", asciiOnly: true));
        Assert.Equal("[1]", WJson.Minify("[1, /* x */]", options: ParseOptions.Jsonc));
        Assert.Throws<JsonParseException>(() => WJson.Format("[1,"));
    }

    [Fact]
    public void Query_with_json_pointer()
    {
        const string json = "{\"items\": [{\"name\": \"uno\"}, {\"name\": \"dos\"}], \"a/b\": 1}";
        Assert.True(WJson.TryQuery(json, "/items/1/name", out var name));
        Assert.Equal("dos", name);
        Assert.True(WJson.TryQuery(json, "/a~1b", out var ab));
        Assert.Equal(1L, ab);
        Assert.False(WJson.TryQuery(json, "/items/5", out var missing));
        Assert.Null(missing);
    }

    [Fact]
    public void Files_in_utf8_and_utf16()
    {
        var dir = Path.Combine(Path.GetTempPath(), "wjson-tests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        try
        {
            var utf16 = Path.Combine(dir, "unicode \u00f1.json");
            File.WriteAllText(utf16, "{\"ok\": true}", new UnicodeEncoding(bigEndian: false, byteOrderMark: true));
            var obj = Assert.IsType<JsonObject>(WJson.ParseFile(utf16));
            Assert.Equal(true, obj["ok"]);
            Assert.True(WJson.TryValidateFile(utf16, out _));

            var bad = Path.Combine(dir, "bad.json");
            File.WriteAllText(bad, "[1,\n2,,]");
            Assert.False(WJson.TryValidateFile(bad, out var error));
            Assert.Equal(2, error!.Line);

            Assert.Throws<FileNotFoundException>(() => WJson.ParseFile(Path.Combine(dir, "missing.json")));
            Assert.False(WJson.TryValidateFile(Path.Combine(dir, "missing.json"), out error));
            Assert.Equal(JsonErrorCode.IoError, error!.Code);
        }
        finally
        {
            Directory.Delete(dir, recursive: true);
        }
    }

    [Fact]
    public void Empty_and_null_inputs()
    {
        Assert.Throws<JsonParseException>(() => WJson.Parse(""));
        Assert.Throws<ArgumentNullException>(() => WJson.Parse(null!));
        Assert.Null(WJson.Parse("null"));
    }
}
