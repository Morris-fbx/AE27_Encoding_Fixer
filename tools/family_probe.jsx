// ===========================================================================
//  family_probe.jsx —— 把一个效果列表的属性名全部导出成 JSON
//
//  用法：在 AE 26（基准）和 AE 27（待测）里各跑一次，
//        再用 tools/compare_probe.py 比对两个 JSON。
//
//  怎么运行：
//    AE 菜单  文件 > 脚本 > 运行脚本文件...  选中本文件即可。
//    （自动化：从任意启动脚本里 evalFile 它。）
//
//  输出：%TEMP%/ae_probe.json
//        想换位置就先设置 $.global.PROBE_OUT = '完整路径'
//
//  要覆盖更多效果，改下面的 PROBE_LIST；
//  第二列是 matchName，也就是传给 layer.Effects.addProperty() 的字符串。
// ===========================================================================
(function () {
    var PROBE_LIST = $.global.PROBE_LIST || [
        ['Adobe GradientWipe',      'ADBE Gradient Wipe'],
        ['Adobe LinearWipe',        'ADBE Linear Wipe'],
        ['Trapcode 3DStroke',       'tc 3DStrokePath'],
        ['Trapcode Shine',          'tc Shine'],
        ['Trapcode Starglow',       'tc Starglow'],
        ['Trapcode Geo',            'Geo'],
        ['Trapcode Form',           'tc Form'],
        ['Trapcode Particular',     'tc Particular'],
        ['Trapcode Tao',            'tc Tao'],
        ['Trapcode Mir',            'rg Mir'],
        ['Trapcode Lux',            'tc Lux'],
        ['Trapcode SoundKeys',      'tc Sound Keys'],
        ['Trapcode Echospace',      'tc Echospace'],
        ['Trapcode Horizon',        'tc Trapcode Horizon'],
        ['RG VFX Bang',             'rgBang'],
        ['MB Looks',                'MB LookSuite3'],
        ['MB Colorista',            'RG MB COLORISTA IV'],
        ['Universe ChromaticGlow',  'Universe_Glow_Chromatic_Glow'],
        ['Boris BCC3Blur',          'BCC3Blur'],
        ['BCC+Glow Darks',          'BCC_GLOWDARKS'],
        ['BCC+Glow',                'BCC_GLOW'],
        ['BCC+Blur',                'BCC_BLUR'],
        ['Sapphire S_Glow',         'S_Glow'],
        ['PluginEverything DeepGlow2', 'PEDG2'],
        ['Aquon',                   'Aquon']
    ];

    function jsonString(value) {
        var text = String(value), out = '"';
        for (var i = 0; i < text.length; i++) {
            var ch = text.charAt(i), code = text.charCodeAt(i);
            if (ch === '"') out += '\\"';
            else if (ch === '\\') out += '\\\\';
            else if (code < 0x20) out += '\\u' + ('0000' + code.toString(16)).slice(-4);
            else out += ch;
        }
        return out + '"';
    }

    function outputPath() {
        if ($.global.PROBE_OUT) return $.global.PROBE_OUT;
        var folder = new Folder(Folder.temp.fsName);
        return folder.fsName + '/ae_probe.json';
    }

    var records = [];
    app.beginUndoGroup('encoding probe');
    var comp = app.project.items.addComp('ENCODING_PROBE', 1280, 720, 1, 1, 24);
    var solid = comp.layers.addSolid([0, 0, 0], 'probe', 1280, 720, 1);

    for (var t = 0; t < PROBE_LIST.length; t++) {
        var label = PROBE_LIST[t][0], matchName = PROBE_LIST[t][1];
        var record = { label: label, match: matchName, ok: false, shown: '', error: '', names: [] };
        try {
            var effect = solid.Effects.addProperty(matchName);
            record.ok = !!effect;
            if (effect) {
                record.shown = effect.name;
                for (var i = 1; i <= effect.numProperties; i++) {
                    var param = effect.property(i);
                    record.names.push(param.name);
                    // one level of groups is enough to catch every label
                    try {
                        if (param.numProperties) {
                            for (var j = 1; j <= param.numProperties; j++) {
                                record.names.push(param.property(j).name);
                            }
                        }
                    } catch (inner) { /* 该属性没有子项 */ }
                }
                try { effect.remove(); } catch (removeError) { /* 移除失败不影响后续 */ }
            }
        } catch (error) {
            record.error = String(error);
        }
        records.push(record);
    }

    app.endUndoGroup();

    var json = '{"app":"' + app.version + '","items":[';
    for (var k = 0; k < records.length; k++) {
        var r = records[k];
        if (k) json += ',';
        json += '{"label":' + jsonString(r.label) +
                ',"match":' + jsonString(r.match) +
                ',"ok":' + (r.ok ? 'true' : 'false') +
                ',"shown":' + jsonString(r.shown) +
                ',"error":' + jsonString(r.error) +
                ',"names":[';
        for (var m = 0; m < r.names.length; m++) {
            if (m) json += ',';
            json += jsonString(r.names[m]);
        }
        json += ']}';
    }
    json += ']}';

    var file = new File(outputPath());
    file.encoding = 'UTF-8';
    if (file.open('w')) {
        file.write(json);
        file.close();
        alert('探测结果已写入：\n' + file.fsName);
    } else {
        alert('写入失败：' + file.fsName);
    }
})();

