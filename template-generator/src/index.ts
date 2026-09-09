import fs from "fs";

function getHtmlFiles(): string[] {
    let files = fs.readdirSync('main', { withFileTypes: true, recursive: true });
    return files.filter(file => file.isFile() && file.name.endsWith('.html')).map(file => file.parentPath + '\\' + file.name);
}

function prepareFormatter(formatterCode: string, index: number): string {
    // data->age | %d 
    const parts = formatterCode.split('|').map(e => e.trim());
    if (parts[1] === '%s') {
        return `cb(cb_context, ${parts[0]});\r\n`;
    }
    const code: string[] = [];
    const varName = `buf${index}`;
    code.push('\r\n');
    code.push(`char ${varName}[20];\r\n`);
    code.push(`sprintf(${varName}, "${parts[1]}", ${parts[0]});\r\n`);
    code.push(`cb(cb_context, ${varName});\r\n`);
    code.push('\r\n');
    return code.join('');
}

function compileTemplate() {
    const templateFiles = getHtmlFiles();
    templateFiles.forEach(templateFileName => {
        const newName = templateFileName.replace('.html', '.c');
        const data = fs.readFileSync(templateFileName, 'utf-8');
        const parser = /((?:\{%|\{\{)[\s\S]+?(?:%\}|\}\}))([^\{]*)/g;
        const parsed = data.match(parser);
        const result: string[] = [];
        parsed?.forEach((e, i) => {
            const parts = e.match(/^(?:\{%|\{\{)([\s\S]+?)(?:%\}|\}\})([^\{]*)$/)?.slice(1) || [];
            if (e[1] == '%') {
                result.push((parts[0] + '\r\n'));
            } else {
                result.push(prepareFormatter(parts[0] || '', i));
            }
            if (parts[1] !== '')
                result.push(`cb(cb_context, ${JSON.stringify(parts[1])});\r\n`);

        }) || [];
        fs.writeFileSync(newName, result?.join(''));
    });
}

compileTemplate();