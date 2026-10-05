/* mio：只栅格化项目SVG。渲染器路径由命令行传入，不在资源里固化
 * 某台机器的缓存路径。壁纸母版3840×2160是真实数学图形的栅格化，
 * 不是把生成器返回的1672×941照片放大后宣称获得4K细节。 */
const fs=require('node:fs');
const path=require('node:path');
const sharp=require(process.argv[2]);
const directory=path.resolve(__dirname,'../assets/design');
(async()=>{
  for(const name of ['AURORA-DUNE','CLASSIC-DUNE','SANDCORE-MARK']){
    const input=fs.readFileSync(path.join(directory,name+'.svg'));
    await sharp(input).png().toFile(path.join(directory,name+'.png'));
    if(name!=='SANDCORE-MARK') await sharp(input).resize(1280,720).png().toFile(path.join(directory,name+'-preview.png'));
    console.log(name);
  }
})().catch(error=>{console.error(error);process.exit(1);});
